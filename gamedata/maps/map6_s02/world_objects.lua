-- map6_s02 (Lakeside Pier and Lighthouse): per-tick rules of the world update:
-- the pier's collision trigger bits and the ring-of-sectors counter that drives them, the Alessa
-- light and its hum when the player stands in the lighthouse cell, and the lake effect texture setup.
-- post_init is the rest of the map's set-up, post_camera the map's per-frame hook after the camera:
-- the flashlight, the light and the stair camera of the pier's spiral.
local CELL = 163840 -- 40 m

-- The original's 40 m chunk test truncates toward zero; positive and non-positive positions take different
-- branches, which the callers below fold into the only cells that can match.
local function cell(v)
    if v >= 0 then return v // CELL end
    return -((-v) // CELL)
end

-- State of the original (reset when the map loads): sector counter, hum/light flag, hum sway angle (s16).
local sector, light_on, sway = 0, 0, 0

local function s16(v)
    v = v & 0xFFFF
    return v >= 0x8000 and v - 0x10000 or v
end

local SFX_HUM = 1611
local BGM_RADIO = 64 -- BgmStatusFlag_6

-- After the objects: the Alessa scene off, the light free of any bone, the water on, the map effects of
-- this map, and M6S02_FlashlightRestorePending for the next map (map6_s00 turns the flashlight back on).
function post_init()
    sh.map.set("alessa_scene_active", 0)
    sh.light.bone_detach()
    sh.light.lens_flare_detach()
    sh.world.water_enable()
    sh.map_effects_init(4, 4)
    sh.flag_set("M6S02_FlashlightRestorePending", true)
end

-- Frame hook state (reset when the map loads): the player entered the spiral cell, and the last stair
-- camera branch (1 at the stairs, 0 at the top, -1 none since the entry), which decides the camera warps.
local spiral_entered, stair_branch = false, -1

-- The camera of the spiral stair: from the top a fixed shot, lower down a shot that follows the player's
-- height. While the player looks around in Self View the camera goes back to the auto camera instead.
local function stair_camera(entry)
    if entry then
        stair_branch = -1
    end
    local _, y, _ = sh.player_pos()
    local branch, cx, cy, cz, wx, wy, wz
    if y > 13107 then -- Q12(3.2)
        branch = 0
        cx, cy, cz = -78028, 5775, -75612 -- Q12(-19.05, 1.41, -18.46)
        wx, wy, wz = -81305, 14172, -89292 -- Q12(-19.85, 3.46, -21.8)
    else
        branch = 1
        local h = math.max(y - 14581, -39526) -- Q12(3.56), Q12(-9.65)
        cx, cy, cz = -81920, h, -81960 -- Q12(-20.0), Q12(-20.01)
        wx, wy, wz = -81920, h + 8192, -81879 -- Q12(-20.0), +Q12(2.0), Q12(-19.99)
    end
    if sh.camera.view_button_active() ~= 0 and sh.camera.self_view_enabled() ~= 0 then
        sh.camera_auto_return(sh.camera.view_button_was_active() == 0)
    else
        local warp = branch ~= stair_branch
        sh.vc_cam_target(cx, cy, cz, warp)
        sh.vc_watch_target(wx, wy, wz, warp)
        if warp then
            sh.camera.exec()
        end
    end
    stair_branch = branch
end

-- After the camera moves, before the player updates: in the spiral cell (x and z in -40..0 m) the
-- flashlight on at the entry, the light brighter near the top, and the light circling the spiral's axis
-- (-20, -20) ahead of the player.
function post_camera()
    local x, y, z = sh.player_pos()
    if not (x <= 0 and x > -CELL and z <= 0 and z > -CELL) then
        spiral_entered = false
        return
    end
    if not spiral_entered then
        sh.light.flashlight_on()
        stair_camera(true)
        spiral_entered = true
    end
    local angle = 0
    if y > 11632 then -- Q12(2.84)
        sh.light.set_intensity((y - 11632) * 1638 // 4752 + 2867) -- Q12(0.4) / (Q12(1.16) + 1) + Q12(0.7)
    else
        sh.light.set_intensity(2867) -- Q12(0.7)
        angle = sh.math.atan2(x + 81920, z + 81920) -- Q12(20)
    end
    stair_camera(false)
    local around = angle + 796 -- Q12_ANGLE(70)
    sh.light.set_pos_x(sh.math.sin(around) * 2252 // 4096 - 81920) -- Q12(0.55), Q12(20)
    sh.light.set_pos_y(math.min(y - 6553, 2048)) -- Q12(1.6), Q12(0.5)
    sh.light.set_pos_z(sh.math.cos(around) * 2252 // 4096 - 81920)
    sh.light.set_rot_x(-455) -- Q12_ANGLE(-40)
    sh.light.set_rot_y(angle - 1706) -- Q12_ANGLE(150)
    sh.light.set_rot_z(0)
end

-- Returns the collision trigger bits for the pier cell.
local function sector_flags(x, z)
    local angle = sh.math.atan2(x + 81920, z + 81920) -- Q12(20)
    if sector == 0 then
        sh.flag_set("M6S02_LighthouseStairAtPierDoor", true)
        if angle > 0 and angle < 1024 then sector = 1 end
        return 258
    end
    if sector & 1 ~= 0 then
        if angle < -896 then -- Q12_ANGLE(-78.8)
            sector = sector + 1
        elseif (sector == 1 and angle > 1024) or angle > 896 then
            sector = sector - 1
        end
    else
        if angle < 0 and angle > -896 then
            sector = sector - 1
        elseif angle > 0 and angle < 896 then
            sector = sector + 1
        end
        if sector == 6 and angle < -1792 then -- Q12_ANGLE(-157.5)
            sh.flag_set("M6S02_LighthouseStairFarEndReached", true)
        end
    end
    sh.flag_set("M6S02_LighthouseStairAtPierDoor", false)
    return (1 << (sector & 31)) + 0x80
end

function update(dt)
    local x, _, z = sh.player_pos()
    local flags = 0

    if x <= 0 and cell(x) == 0 and z <= 0 and cell(z) == 0 then
        flags = sector_flags(x, z)
    else
        sh.flag_set("M6S02_LighthouseStairFarEndReached", false)
        sh.flag_set("M6S02_LighthouseRoomWarpDone", false)
    end

    if x > 0 and cell(x) == 1 and z <= 0 and cell(z) == 0 then
        local dx, dz = x - 245760, z + 81920 -- Q12(60), Q12(20)
        if sh.math.vec2_len_q6(dx, dz) < 5734 then -- Q12(1.4)
            if sh.math.atan2(dx, dz) > 768 then -- Q12_ANGLE(67.5)
                sh.flag_set("M6S02_LighthouseRoomStairEntered", true)
                sector = 6
            end
        end

        if light_on == 0 then
            sh.light.set_intensity(11468) -- Q12(2.8)
            sh.light.set_pos_x(245760)
            sh.light.set_pos_y(-10240)
            sh.light.set_pos_z(-81920)
            sh.light.set_rot_x(-1024)
            sh.light.set_rot_y(0)
            sh.light.set_rot_z(0)
            sh.map6_s02.lake_effect_init(0)
            sh.sd_call(SFX_HUM)
            light_on = 1
        elseif sh.sound.bgm_flags() & BGM_RADIO ~= 0 then
            sh.map6_s02.lake_effect_init(1)
            light_on = 1
        elseif light_on == 1 then
            light_on = 2
            sh.map.set("map_effect_flags", sh.map.get("map_effect_flags") | 32)
        end

        sway = s16(sway + ((sh.rng_uint(0, 4095) * dt) >> 12))
        sh.sfx_attributes_update(SFX_HUM, 0, (sh.math.sin(sway) // 1024) - 32, -64)
    else
        if light_on ~= 0 then
            sh.sfx_stop(SFX_HUM)
            light_on = 0
            sh.map.set("map_effect_flags", sh.map.get("map_effect_flags") & ~32)
        end
        sh.flag_set("M6S02_LighthouseRoomStairEntered", false)
        sh.flag_set("M6S02_LighthouseStairWarpDone", false)
    end

    sh.collision.flag_bits_clear(0xFFFF)
    sh.collision.flag_bits_set(flags)
end
