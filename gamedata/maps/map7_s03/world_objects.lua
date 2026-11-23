-- World objects of map7_s03: the ending's bin, nu and baby and
-- the scrolling real, ura and under worlds. The scenes of events.lua set the map variables that show them.
-- The objects are drawn where the original draws the bin; post_draw() follows. post_init is the rest of the map's
-- set-up, post_camera the per-frame hook after the camera (the final bosses' effects), final_boss_camera the fight
-- camera, also called by events.lua through sh.world_objects.call.

local WORLD_START_Z = -573440 -- Q12(-140), the first copy's z at scroll 0
local COPY_SHIFT = {0, -81920, 81920} -- copies 0, 1, 2: 0, -20 m, +20 m
local WORLD_PERIOD = 81920 -- Q12(20)
local SCROLL_PER_SECOND = 10936 -- Q12(2.67)
local BIN_SPIN_DIVISOR = 14745 -- Q12(3.6)
local WORLD_X = 409600 -- Q12(100)

-- The worlds' scroll starts at 0 when the map loads.
local scroll = 0

local function s16(v)
    v = v & 0xFFFF
    return v >= 0x8000 and v - 0x10000 or v
end

function update(dt)
    local obj = sh.world_object

    -- The bin turns about x while it is drawn; the angle is the s16 map variable bin_spin. Its y and z
    -- turns are zero (the original wrote x and y through one s32).
    local bin = sh.map.get("update_bin") ~= 0
    obj.visible_set("Bin", bin)
    if bin then
        local spin = sh.map.get("bin_spin")
        obj.rotation_set("Bin", spin, 0, 0)
        sh.map.set("bin_spin", s16(spin - (dt * 4096) // BIN_SPIN_DIVISOR))
    end

    obj.visible_set("Nu", sh.map.get("update_nu") ~= 0)
    obj.visible_set("Baby", sh.map.get("update_baby") ~= 0)

    -- The worlds scroll while the real or the ura world is shown (the ura world's mode 2 holds it still);
    -- the under world only follows them.
    local real = sh.map.get("real_world_shown") ~= 0
    local ura_mode = sh.map.get("ura_world_shown")
    local under = sh.map.get("under_world_shown") ~= 0
    local active = real or ura_mode ~= 0
    if active then
        if ura_mode ~= 2 then
            -- Q12_MULT_PRECISE: the arithmetic shift right is a floor division.
            scroll = scroll + (dt * SCROLL_PER_SECOND) // 4096
        end
        if scroll > WORLD_PERIOD then
            scroll = scroll - WORLD_PERIOD
        end
    end
    for i = 0, 2 do
        local z = WORLD_START_Z - scroll + COPY_SHIFT[i + 1]
        for _, name in ipairs({"Real", "Ura", "Under"}) do
            obj.position_set(name .. i, WORLD_X, 0, z)
        end
        obj.visible_set("Real" .. i, active and real)
        obj.visible_set("Ura" .. i, active and ura_mode ~= 0)
        obj.visible_set("Under" .. i, active and under)
    end
end

-- The character slots of the player and the bosses (npcs[2] the Incubus, npcs[4] the Incubator), and the
-- collision trigger bits of the scrolling worlds.
local PLAYER, INCUBUS_SLOT, INCUBATOR_SLOT = 0, 3, 5
local COLLISION_ALL, COLLISION_REAL, COLLISION_URA = 0xFFFF, 2, 4

-- After the objects (the rest of the map's set-up): the ending step back to 0, the light free of
-- any bone above the arena, pointing down, the boss effects set up, and the ending this run is heading for
-- (1/2: Cybil saved or not, +2 when Kaufmann did not take the vial).
function post_init()
    sh.map.set("ending_step", 0)
    sh.light.bone_detach()
    sh.light_pos_set(572211, -18432, -401817) -- Q12(139.7, -4.5, -98.1)
    sh.light.lens_flare_detach()
    sh.light_rot_set(-1024, 0, 0) -- -90 degrees about x
    sh.light_intensity_set(8192) -- Q12(2.0)
    sh.map.set("boss_fx_time", 0)
    sh.map.thunder_gaps_reset()
    sh.map.boss_bolts_load()
    sh.map.boss_fx_init()
    sh.map.set("lightning_hidden", 0)
    sh.map.set("ending_flare", 0)
    local ending = sh.flag_get("M6S04_CybilSaved") and 1 or 2
    if not sh.flag_get("M5S03_KaufmannHasMotorcycleVial") then
        ending = ending + 2
    end
    sh.map.set("ending_idx", ending)
end

-- The fight camera: behind the player, turned toward the boss, while the player holds the view control (not in
-- Self View); otherwise the auto camera. warp (0/1) places it at once. The shifts of the original are
-- arithmetic, so they are floor divisions here.
function final_boss_camera(player, other, warp)
    warp = warp ~= 0
    local px, pz = sh.npc.pos_x(player), sh.npc.pos_z(player)
    local ox, oz = sh.npc.pos_x(other), sh.npc.pos_z(other)
    local angle = sh.math.atan2(px - ox, pz - oz)
    if sh.camera.view_button_active() == 0 or sh.camera.self_view_enabled() ~= 0 then
        sh.camera_auto_return(false)
        return
    end
    local turn = sh.npc.rot_y(player)
    local incubus = sh.npc.chara_id(other) == sh.chara_id("Incubus")
    local cx = px + (sh.math.sin(turn) * 6144) // 4096 + (sh.math.sin(angle) * 10240) // 4096 -- Q12(1.5), Q12(2.5)
    local cz = pz + (sh.math.cos(turn) * 6144) // 4096 + (sh.math.cos(angle) * 10240) // 4096
    local cy = incubus and -5734 or -9420 -- Q12(-1.4), Q12(-2.3)
    local lx = ox + sh.math.sin(angle)
    local lz = oz + sh.math.cos(angle)
    local ly
    if warp then
        ly = -8 -- Q12(-0.002)
    elseif incubus then
        ly = sh.camera.look_at_height()
    else
        ly = -3276 -- Q12(-0.8)
    end
    sh.vc_cam_target(cx, cy, cz, warp)
    sh.vc_watch_target(lx, ly, lz, warp)
    if warp then
        sh.camera.exec()
    end
end

-- After the draws (the rest of the map's world update): the fight camera on the boss of this ending while the
-- fight is on, and the collision of the scrolling world that is shown.
function post_draw()
    if sh.flag_get("M7S03_FinalBossIntroDone") and not sh.flag_get("M7S03_IncubusKilled") and not sh.flag_get("M7S03_IncubatorKilled") then
        local boss = sh.flag_get("M5S03_KaufmannHasMotorcycleVial") and INCUBUS_SLOT or INCUBATOR_SLOT
        final_boss_camera(PLAYER, boss, 0)
    end
    local real = sh.map.get("real_world_shown") ~= 0
    local ura = sh.map.get("ura_world_shown") ~= 0
    if real or ura then
        sh.collision.flag_bits_clear(COLLISION_ALL)
        if real then
            sh.collision.flag_bits_set(COLLISION_REAL)
        end
        if ura then
            sh.collision.flag_bits_set(COLLISION_URA)
        end
    end
end

-- After the camera moves (the per-frame hook): the light beams, then while the boss effects run the thunder
-- pacing and the lightning cloud (unless hidden) and the fire, then the glow at a fixed point.
function post_camera()
    sh.map.boss_fx_update()
    if sh.map.get("boss_fx_time") ~= 0 then
        if sh.map.get("lightning_hidden") == 0 then
            sh.map.thunder_gaps_tick()
            sh.map.lightning_update()
        end
        sh.map.boss_fire_update()
    end
    local flare = sh.map.get("ending_flare")
    if flare > 0 then
        sh.map.ending_flare_draw(flare - 1)
    end
end
