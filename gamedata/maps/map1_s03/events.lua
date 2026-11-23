-- map1_s03: School Otherworld - 2F and roof
-- Event scripts of this area: the engine runs a handler every tick while its event is active.
-- Fixed-point numbers: 4096 = 1.0 (metres, seconds); angles: 4096 = 360 degrees.

function on_boot()
    -- per-map script entry point
end

-- index 0: jammed door.
function MapEvent_DoorJammed()
    sh.event_msg_with_sfx("common.lock_jammed", "door_jammed")
end

-- index 1: locked door.
function MapEvent_DoorLocked()
    sh.event_msg_with_sfx("common.locked", "door_locked")
end

-- index 2: unlocked door.
function MapEvent_DoorUnlocked()
    sh.event_msg_with_sfx("common.unlocked", "door_unlocked")
end

-- index 3: library reserve key used: the key is removed, a message with the key sound,
-- and the library reserve is marked on the map.
function UseLibraryReserveKey()
    sh.item_remove("LibraryReserveKey", 1)
    sh.event_msg_with_sfx("map1_s03.used_the_library_reserve_key", "use_key") -- message map1_s03.used_the_library_reserve_key "Used the Library reserve key." / use key sound
    sh.flag_set("MapMark_AltSchool2F_LibraryReserveArrows", true) -- map mark: Otherworld school 2F library reserve arrows
end

-- index 4: classroom key used: the key is removed and a message plays with the key sound.
function UseClassroomKey()
    sh.item_remove("ClassroomKey", 1)
    sh.event_msg_with_sfx("map1_s03.used_the_classroom_key", "use_key") -- message map1_s03.used_the_classroom_key "Used classroom key." / use key sound
end

-- index 5: common pickups, chosen by the event's point of interest.
function TakeCommonPickup()
    local poi = sh.event_point()
    local t = {
        [84] = {"ShotgunShells","M1S03_ShotgunShells0"},
        [85] = {"HandgunBullets","M1S03_HandgunBullets0"},
        [86] = {"HealthDrink","M1S03_HealthDrink0"},
        [87] = {"HandgunBullets","M1S03_HandgunBullets1"},
        [88] = {"HandgunBullets","M1S03_HandgunBullets2"},
        [89] = {"ShotgunShells","M1S03_ShotgunShells1"},
        [90] = {"HandgunBullets","M1S03_HandgunBullets3"},
        [91] = {"HealthDrink","M1S03_HealthDrink1"},
        [92] = {"HealthDrink","M1S03_HealthDrink2"},
        [93] = {"FirstAidKit","M1S03_FirstAidKit"},
    }
    local e = t[poi]
    if e == nil then
        -- any other point gives a first aid kit and sets no flag
        e = {"FirstAidKit", 0}
    end
    sh.event_common_item_take(e[1], e[2])
end

-- index 6: inspect the courtyard drain pipe (message by flags and activation type).
-- Activation type 3 = an item was used on the trigger.
function InspectDrainPipe()
    local step = sh.event_step_get(0)
    if step == 0 then
        sh.player_freeze(true)
        sh.event_step_increment(0)
        step = 1
    end
    if step == 1 then
        sh.event_wait_player_stop()
    elseif step == 2 then
        sh.chara_anim_play_to_end(0, "pickup_crouch")                      -- bend down
    elseif step == 3 then
        local poi = sh.event_point()
        if poi == 19 or poi == 20 then
            if sh.event_activation_type_get() == 3 then       -- used an item
                sh.flag_set("M1S02_UsedRubberBall", true)
                sh.event_msg(false, "map1_s03.used_the_rubber_ball", 0, 0, 0, false)       -- "Used rubber ball."
            else
                sh.event_msg(false, "map1_s03.nothing_special", 0, 0, 0, false)       -- "Nothing special."
            end
        elseif poi == 17 or poi == 18 then
            if sh.flag_get("M1S02_PickupClassroomKey") then
                sh.event_msg(false, "map1_s03.nothing_special", 0, 0, 0, false)       -- "Nothing special."
            else
                sh.event_msg(false, "map1_s03.the_key_went_down_the", 0, 0, 0, false)       -- "There's a hanging key."
            end
        end
    elseif step == 4 then
        sh.chara_anim_play_to_end(0, "pickup_stand_up")                      -- straighten up
    else
        sh.player_freeze(false)
        sh.state_set(0)
    end
end

-- index 7: key in the courtyard drain: inspect with a background image.
function InspectDrainHole()
    local step = sh.event_step_get(0)
    if step == 0 then
        sh.player_freeze(true)
        local poi = sh.event_point()
        if poi == 17 or poi == 18 then
            if sh.flag_get("M1S03_KeyDownTheDrain") then
                sh.event_bg_texture_cmd(0, "TIM/HOLEKEY1.TIM", false) -- QueueRead
            else
                sh.event_bg_texture_cmd(0, "TIM/HOLEKEY0.TIM", false)
                sh.flag_set("M1S03_KeyInDrainSeen", true)
            end
        elseif poi == 19 or poi == 20 then
            if sh.event_activation_type_get() == 3 or sh.flag_get("M1S02_UsedRubberBall") then -- Item
                sh.event_bg_texture_cmd(0, "TIM/BALLHOLE.TIM", false)
            else
                sh.event_bg_texture_cmd(0, "TIM/BALLHOL1.TIM", false)
            end
        end
        sh.event_step_increment(0)
        step = 1
    end
    if step == 1 then
        sh.event_wait_player_stop()
    elseif step == 2 then
        sh.chara_anim_play_to_end(0, "pickup_crouch")                      -- bend down
    elseif step == 3 then
        sh.event_fade_cmd(0, true, 0, 0, false)               -- Start, out
        sh.event_step_increment(0)
        step = 4 -- continues into step 4 in the same tick
    end
    if step == 4 then
        sh.event_bg_texture_cmd(1, 0, false)                  -- AwaitLoad
    elseif step == 5 then
        sh.event_fade_cmd(1, true, 0, 0, false)               -- Wait, out
    elseif step == 6 then
        sh.event_bg_texture_cmd(2, 0, false)                  -- Draw
        sh.event_fade_cmd(2, false, 0, 0, false)              -- Auto, in
    elseif step == 7 then
        sh.event_bg_texture_cmd(2, 0, false)
        local poi = sh.event_point()
        if poi == 19 or poi == 20 then
            if sh.event_activation_type_get() == 3 then       -- used an item
                sh.flag_set("M1S02_UsedRubberBall", true)
                sh.event_msg(false, "map1_s03.used_the_rubber_ball", 0, 0, 0, false)       -- "Used rubber ball."
            elseif sh.flag_get("M1S02_UsedRubberBall") then
                sh.event_msg(false, "map1_s03.the_ball_is_stuffed_inside", 0, 0, 0, false)       -- "The ball is stuffed inside the pipe."
            else
                sh.event_msg(false, "map1_s03.nothing_special", 0, 0, 0, false)       -- "Nothing special."
            end
        elseif sh.flag_get("M1S02_PickupClassroomKey") then
            sh.event_msg(false, "map1_s03.nothing_special", 0, 0, 0, false)           -- "Nothing special."
        elseif not sh.flag_get("M1S03_KeyDownTheDrain") then
            sh.event_msg(false, "map1_s03.theres_a_hanging_key_its", 0, 0, 0, false)           -- "There's a hanging key."
        else
            sh.event_msg(false, "map1_s03.the_key_went_down_the", 0, 0, 0, false)           -- "The key went down the drain."
        end
    elseif step == 8 then
        sh.event_bg_texture_cmd(2, 0, false)
        sh.event_fade_cmd(2, true, 0, 0, false)               -- Auto, out
    elseif step == 9 then
        sh.event_fade_cmd(2, false, 0, 0, false)              -- Auto, in
    elseif step == 10 then
        sh.chara_anim_play_to_end(0, "pickup_stand_up")                      -- straighten up
    elseif step < 0 or step > 10 then -- default
        sh.player_freeze(false)
        sh.state_set(0)
    end
end

-- index 8: the drainage valve on the roof: "Do you want to open it?"; Harry turns the valve
-- open (animation push_object_alt), the water runs along the gutter (water_stream with its
-- drip splashes) while the camera and the light follow it along valve_light_pos/look; with the
-- rubber ball in the pipe the water takes the third segment and carries the key down the
-- drain (M1S03_KeyDownTheDrain), otherwise "It drains into the wrong hole."; then Harry turns
-- the valve shut (lift_platform_alt). While Harry turns it, the valve follows his right hand
-- and squeaks; the water sound's volume follows how far it is open and the camera's distance.
local HarryBoneRightHand = 10
local ValveSfxPos = {-43008, -2048, -52838}   -- (-10.5, -0.5, -12.9)
local WaterSfxPos = {-41779, 0, -54681}       -- (-10.2, 0.0, -13.35)

-- C integer division (truncates toward zero).
local function ValveTDiv(a, b)
    local q = a // b
    if q < 0 and q * b ~= a then
        q = q + 1
    end
    return q
end

local function ValveS16(v)
    v = v & 0xFFFF
    return v >= 0x8000 and v - 0x10000 or v
end

local function ValveS32(v)
    v = v & 0xFFFFFFFF
    return v >= 0x80000000 and v - 0x100000000 or v
end

-- Math_AngleNormalizeSigned: the angle wrapped into -180..180 degrees (4096 = 360).
local function ValveAngleSigned(a)
    local n = a & 0xFFF
    return n > 2048 and n - 4096 or n
end

-- Math_Vector3MagCalcSafe: the length with the components reduced to 1/64 first.
local function ValveMag3(x, y, z)
    local x6, y6, z6 = x // 64, y // 64, z // 64
    return sh.math.sqrt(x6 * x6 + y6 * y6 + z6 * z6) * 64
end

local function StreamSet(field, segment, value)
    sh.map.set("water_stream." .. field, value, segment)
end

local function DripsSet(field, emitter, value)
    sh.map.set("water_drips." .. field, value, emitter)
end

local function ValveLightKey(name, idx)
    return sh.map.get(name .. ".x", idx), sh.map.get(name .. ".y", idx), sh.map.get(name .. ".z", idx)
end

-- valve_light_pos/look between key idx - 1 and idx at the light clock (keys i - 1 and i).
local function ValveLightLerp(name, idx, clock, i)
    local start_time = sh.map.get("valve_light_times", i - 1)
    local span = sh.map.get("valve_light_times", i) - start_time
    local ax, ay, az = ValveLightKey(name, idx - 1)
    local bx, by, bz = ValveLightKey(name, idx)
    return ax + ValveTDiv(ValveS32((bx - ax) * (clock - start_time)), span),
           ay + ValveTDiv(ValveS32((by - ay) * (clock - start_time)), span),
           az + ValveTDiv(ValveS32((bz - az) * (clock - start_time)), span)
end

local function ValveCameraHome()
    sh.camera.position(nil, -50708, -4915, -57630, 0, 0, 0, 0, true) -- (-12.38, -1.2, -14.07)
    sh.camera.look_at(nil, -37847, -3112, -47636, 0, 0, 0, 0, true)  -- (-9.24, -0.76, -11.63)
end

-- The water: the stream's segments and the drip splashes, as the scene sets them up.
local function ValveWaterSetup(usedBall)
    StreamSet("x", 0, -41779)          -- -10.2
    StreamSet("y", 0, -1433)           -- -0.35
    StreamSet("z", 0, -54681)          -- -13.35
    StreamSet("length", 0, 1843)       -- 0.45
    StreamSet("falling", 0, 1)
    StreamSet("ribbon_width", 0, 737)  -- 0.18
    StreamSet("ripple_length", 0, 1024) -- 0.25
    StreamSet("ripple_speed", 0, 6144) -- 1.5
    StreamSet("spray_width", 0, 737)   -- 0.18
    StreamSet("fade_length", 0, 1024)  -- 0.25
    StreamSet("drop_speed", 0, 0)

    StreamSet("x", 1, -41984)          -- -10.25
    StreamSet("y", 1, 409)             -- 0.1
    StreamSet("z", 1, -53166)          -- -12.98
    StreamSet("length", 1, 20152)      -- 4.92
    StreamSet("falling", 1, 0)
    StreamSet("yaw", 1, 2048)          -- 180 deg
    StreamSet("ribbon_width", 1, 1228) -- 0.3
    StreamSet("ripple_length", 1, 2048) -- 0.5
    StreamSet("ripple_speed", 1, 2457) -- 0.6
    StreamSet("spray_width", 1, 1228)  -- 0.3
    StreamSet("fade_length", 1, 2048)  -- 0.5
    StreamSet("drop_speed", 1, 3276)   -- 0.8

    DripsSet("x", 0, -41779)           -- -10.2
    DripsSet("y", 0, 491)              -- 0.12
    DripsSet("radius", 0, 327)         -- 0.08
    DripsSet("z", 0, -54681)           -- -13.35
    DripsSet("shape", 0, 2)
    DripsSet("count", 0, 40)
    DripsSet("size", 0, 1024)          -- 0.25
    DripsSet("lifetime", 0, 1228)      -- 0.3

    DripsSet("x", 1, -41984)           -- -10.25
    DripsSet("z", 1, -72581)           -- -17.72
    DripsSet("radius", 1, 204)         -- 0.05
    DripsSet("count", 1, 40)

    if usedBall then
        StreamSet("x", 2, -41369)          -- -10.1
        StreamSet("y", 2, 532)             -- 0.13
        StreamSet("length", 2, 27934)      -- 6.82
        StreamSet("yaw", 2, 3072)          -- 270 deg
        StreamSet("drop_count", 0, 150)
        StreamSet("z", 2, -72704)          -- -17.75
        StreamSet("falling", 2, 0)
        StreamSet("ribbon_width", 2, 1228) -- 0.3
        StreamSet("ripple_length", 2, 2048) -- 0.5
        StreamSet("ripple_speed", 2, 2457) -- 0.6
        StreamSet("spray_width", 2, 1228)  -- 0.3
        StreamSet("fade_length", 2, 2048)  -- 0.5
        StreamSet("drop_speed", 2, 3276)   -- 0.8
        StreamSet("segment_count", 0, 3)

        DripsSet("y", 1, 901)              -- 0.22
        DripsSet("shape", 1, 4)
        DripsSet("yaw", 1, 0)
        DripsSet("size", 1, 819)           -- 0.2
        DripsSet("lifetime", 1, 1433)      -- 0.35
        DripsSet("radius", 2, 245)         -- 0.06
        DripsSet("x", 2, -69058)           -- -16.86
        DripsSet("z", 2, -72704)           -- -17.75
        DripsSet("y", 2, 819)              -- 0.2
        DripsSet("shape", 2, 3)
        DripsSet("count", 2, 60)
        DripsSet("size", 2, 819)           -- 0.2
        DripsSet("lifetime", 2, 819)       -- 0.2
    else
        StreamSet("drop_count", 0, 80)
        StreamSet("segment_count", 0, 2)
        DripsSet("y", 1, 819)              -- 0.2
        DripsSet("shape", 1, 3)
        DripsSet("size", 1, 614)           -- 0.15
        DripsSet("lifetime", 1, 819)       -- 0.2
    end

    -- The end of the last segment.
    local last = sh.map.get("water_stream.segment_count")
    StreamSet("length", last, 2048)        -- 0.5
    StreamSet("y", last, 655)              -- 0.16
    StreamSet("ribbon_width", last, 409)   -- 0.1
    StreamSet("spray_width", last, 491)    -- 0.12
    StreamSet("fade_length", last, 1228)   -- 0.3
end

-- The light and the camera along the water (step 9).
local function ValveLightFollow()
    sh.map.set("valve_light_clock", sh.map.get("valve_light_clock") + sh.game.delta_time())
    local clock = sh.map.get("valve_light_clock")

    local i = 1
    while i < 6 do
        if clock < sh.map.get("valve_light_times", i) then
            break
        end
        i = i + 1
    end

    if i == 6 or (not sh.flag_get("M1S02_UsedRubberBall") and i > 3) then
        sh.event_step_increment(0)
        return
    end

    local idx = i >> 1
    if sh.flag_get("M1S02_UsedRubberBall") then
        idx = idx + 2
    end

    local lx, ly, lz, ax, ay, az
    if i & 1 ~= 0 then
        lx, ly, lz = ValveLightKey("valve_light_pos", idx)
        ax, ay, az = ValveLightKey("valve_light_look", idx)
    else
        lx, ly, lz = ValveLightLerp("valve_light_pos", idx, clock, i)
        ax, ay, az = ValveLightLerp("valve_light_look", idx, clock, i)
    end

    sh.vec.set(0, lx, ly, lz)
    sh.vec.set(1, ax, ay, az)
    sh.camera.position("vec:0", 0, 0, 0, 0, 0, 0, 0, true)
    sh.camera.look_at("vec:1", 0, 0, 0, 0, 0, 0, 0, true)

    sh.light.lens_flare_detach()
    sh.light.set_rot_x(-sh.math.atan2(ay - ly, sh.math.vec2_len_q6(ax - lx, az - lz)))
    sh.light.set_rot_y(sh.math.atan2(ax - lx, az - lz))
    sh.light.set_rot_z(0)
    sh.light.bone_detach()
    sh.light.set_pos_x(lx)
    sh.light.set_pos_y(math.max(-6144, ly)) -- not above -1.5
    sh.light.set_pos_z(lz)
end

-- While Harry turns the valve (steps 7 and 12): the valve follows the hand; squeaks; the
-- water starts on the second squeak of opening and stops on the second squeak of closing.
local function ValveTurnFollow(step)
    local hx, hy, hz = sh.entity.bone_point(0, HarryBoneRightHand, sh.map.get("valve_hand_offset", 0),
                                           sh.map.get("valve_hand_offset", 1), sh.map.get("valve_hand_offset", 2))
    local angle = sh.math.atan2(hy * 16 + 2867, hz * 16 + 52920) -- around (y -0.7, z -12.92)

    local frame = 0
    if sh.map.get("valve_turn_frames") > 2 then
        frame = sh.npc.anim_time(0) // 4096 - sh.player.anim_something()
    end

    if frame >= 6 and frame < 37 then
        local turn = ValveAngleSigned(sh.map.get("valve_hand_angle") - angle)
        if (step == 7 and turn < 0) or (step == 12 and turn > 0) then
            local squeaks = sh.map.get("valve_squeak_count")
            if squeaks == 0 then
                sh.sfx_flags_play("valve_squeak_first", ValveSfxPos[1], ValveSfxPos[2], ValveSfxPos[3], 128, 0)
                sh.map.set("valve_squeak_count", squeaks + 1)
            elseif squeaks & 1 == 0 and squeaks < 8 then
                sh.sfx_flags_play("valve_squeak", ValveSfxPos[1], ValveSfxPos[2], ValveSfxPos[3], 128, 0)

                if squeaks == 2 then
                    if step == 12 then
                        sh.sfx_stop("valve_water_flow")
                        sh.map.set("valve_water_running", 0)
                    elseif step == 7 then
                        sh.sd_call("valve_water_flow")
                        sh.map.set("valve_water_running", 1)
                    end
                end

                sh.map.set("valve_squeak_count", squeaks + 1)
            end

            -- the valve wheel is the world object DrainValve (world_objects.json); its angles are 16-bit
            local rx, ry, rz = sh.world_object.rotation_get("DrainValve")
            sh.world_object.rotation_set("DrainValve", rx + turn, ry, rz)

            if sh.map.get("valve_water_running") ~= 0 then
                local open = ValveS16(sh.map.get("valve_water_volume") - turn)
                if open < 0 then
                    open = 0
                end
                if open > 0xFF0 then
                    open = 0xFF0
                end
                sh.map.set("valve_water_volume", open)
            end
        elseif sh.map.get("valve_squeak_count") & 1 ~= 0 then
            sh.map.set("valve_squeak_count", sh.map.get("valve_squeak_count") + 1)
        end
    end

    sh.map.set("valve_hand_angle", angle)
end

-- The running water's sound: panned to the gutter, as loud as the valve is open, fading out
-- over 1..17 m from the point between Harry and the camera.
local function ValveWaterSound()
    local balance = sh.vc_stereo_balance(WaterSfxPos[1], WaterSfxPos[2], WaterSfxPos[3]) // 2
    local vx, vy, vz = sh.vc_view_position()
    local px, py, pz = sh.player_pos()
    local dist = ValveMag3((px + vx) // 2 + 41779, (py + vy) // 2, (pz + vz) // 2 + 54681)

    local vol
    if dist < 4096 then
        vol = sh.map.get("valve_water_volume")
    else
        vol = dist
        if vol > 69632 then -- 17.0
            vol = 0
        else
            vol = (sh.map.get("valve_water_volume") * (4096 - ValveTDiv((vol - 4096) << 12, 65536))) // 4096
        end
    end

    vol = ~(vol // 16)
    sh.sfx_attributes_update("valve_water_flow", balance, vol, 0)
end

function MapEvent_DrainageValveInteract()
    local step = sh.event_step_get(0)
    if step == 0 then
        sh.player_freeze(true)

        sh.map.set("valve_water_volume", 0)
        sh.map.set("valve_water_running", 0)

        sh.flag_set("MapMark_AltSchoolRF_ValveSign", true)

        sh.map.set("valve_squeak_count", 0)
        sh.event_step_increment(0)
        step = 1 -- continues into step 1 in the same tick
    end

    if step == 1 then
        sh.event_wait_player_stop()
    elseif step == 2 then
        sh.event_msg(true, "map1_s03.there_is_a_drainage_valve", 4, 3, 0, false) -- yes: 4, no: 3
    elseif step == 3 then
        sh.player_freeze(false)
        sh.state_set(0) -- gameplay
    elseif step == 4 or step == 5 then
        if step == 4 then
            sh.event_fade_cmd(0, true, 2, 0, false)

            ValveWaterSetup(sh.flag_get("M1S02_UsedRubberBall"))

            sh.npc.set_pos_x(0, -46899) -- -11.45
            sh.npc.set_pos_z(0, -53043) -- -12.95
            sh.npc.set_rot_y(0, 1024)   -- 90 deg

            sh.player.held_item_hide()
            sh.player.weapon_stash()

            ValveCameraHome()

            sh.event_step_increment(0)
        end
        sh.event_wait_timer(6144, false) -- 1.5 s
    elseif step == 6 or step == 7 then
        if step == 6 then
            sh.map.set("valve_turn_frames", 0)
            sh.event_step_increment(0)
        end
        sh.map.set("valve_turn_frames", sh.map.get("valve_turn_frames") + 1)
        sh.chara_anim_play_to_end(0, "push_object_alt") -- turn the valve open
    elseif step == 8 or step == 9 then
        if step == 8 then
            sh.map.water_stream_start()
            sh.map.water_drips_start(0)
            sh.map.water_drips_start(1)
            if sh.flag_get("M1S02_UsedRubberBall") then
                sh.map.water_drips_start(2)
            end

            sh.npc.anim_flags_clear(0, 2) -- Harry invisible

            sh.world.water_enable()
            local x, y, z = ValveLightKey("valve_light_pos", 0)
            sh.camera.position(nil, x, y, z, 0, 0, 0, 0, true)
            x, y, z = ValveLightKey("valve_light_look", 0)
            sh.camera.look_at(nil, x, y, z, 0, 0, 0, 0, true)

            sh.map.set("valve_light_clock", 0)
            sh.event_step_increment(0)
        end
        ValveLightFollow()
    elseif step == 10 then
        if sh.flag_get("M1S02_UsedRubberBall") then
            sh.event_step_increment(0)
        else
            sh.event_msg(false, "map1_s03.it_drains_into_the_wrong", 0, 0, 0, false)
        end
    elseif step == 11 or step == 12 then
        if step == 11 then
            sh.npc.anim_flags_set(0, 2) -- Harry visible again

            sh.world.water_disable()
            sh.light.flashlight_fix()

            ValveCameraHome()

            sh.map.water_stream_stop()

            if sh.flag_get("M1S02_UsedRubberBall") then
                sh.flag_set("M1S03_KeyDownTheDrain", true)
            end

            sh.map.set("valve_squeak_count", 0)
            sh.map.set("valve_turn_frames", 0)
            sh.event_step_increment(0)
        end
        sh.map.set("valve_turn_frames", sh.map.get("valve_turn_frames") + 1)
        sh.chara_anim_play_to_end(0, "lift_platform_alt") -- turn the valve shut
    else
        sh.player_freeze(false)
        sh.state_set(0) -- gameplay
        sh.event_fade_cmd(0, false, 2, 0, false)
        sh.camera.auto_return(true)
        sh.player.weapon_restore()
    end

    -- The step may have moved on above.
    step = sh.event_step_get(0)
    if step == 7 or step == 12 then
        ValveTurnFollow(step)
    end

    if sh.map.get("valve_water_running") ~= 0 then
        ValveWaterSound()
    end
end

-- index 9: "The Monster Lurks" book on the library floor.
-- The first read branches to a longer scene; then picture + fade + wait.
function ReadMonsterLurksBook()
    local step = sh.event_step_get(0)
    if step == 0 then
        sh.player_freeze(true)
        sh.event_step_increment(0)
        step = 1
    end
    if step == 1 then
        sh.event_wait_player_stop()
    elseif step == 2 then
        sh.event_sys_state_branch_on_flag("M1S03_MonsterLurksBookRead", 6, 3, false)
    elseif step == 3 then
        sh.event_msg(false, "map1_s03.whats_this", 0, 0, 0, false)               -- "What's this?"
    elseif step == 4 then
        sh.event_wait_timer(2457, false)                      -- 0.6 s
    elseif step == 5 then
        sh.event_msg(false, "map1_s03.the_monster_lurks_is_the", 0, 0, 0, false)               -- "The Monster Lurks is the book's title."
    elseif step == 6 then
        sh.event_bg_texture_cmd(0, "TIM/LMONSTER.TIM", false) -- QueueRead
        sh.event_fade_cmd(0, true, 0, 0, false)               -- Start, out
        sh.event_step_increment(0)
        step = 7 -- continues into step 7 in the same tick
    end
    if step == 7 then
        sh.event_bg_texture_cmd(1, 0, false)                  -- AwaitLoad
    elseif step == 8 then
        sh.event_fade_cmd(1, true, 0, 0, false)               -- Wait, out
    elseif step == 9 then
        sh.event_bg_texture_cmd(2, 0, false)                  -- Draw
        sh.event_fade_cmd(2, false, 0, 0, false)              -- Auto, in
    elseif step == 10 then
        sh.event_bg_texture_cmd(2, 0, false)
        sh.event_msg(false, "map1_s03.chapter_3_manifestation_of", 0, 0, 0, false)               -- "Chapter3: ..."
    elseif step == 11 then
        sh.event_bg_texture_cmd(2, 0, false)
        sh.event_fade_cmd(2, true, 0, 0, false)               -- Auto, out
    elseif step < 0 or step > 11 then -- default
        sh.player_freeze(false)
        sh.state_set(0)
        sh.flag_set("M1S03_MonsterLurksBookRead", true)
        sh.event_fade_cmd(0, false, 0, 0, false)              -- Start, in
    end
end

-- index 10: the old fairy tale on the library counter (LIBRBOOK.TIM).
-- Picture + fade + message; flag M1S03_LibraryFairyTaleRead picks the branch.
function ReadLibraryFairyTale()
    local step = sh.event_step_get(0)
    if step == 0 then
        sh.player_freeze(true)
        sh.event_bg_texture_cmd(0, "TIM/LIBRBOOK.TIM", false) -- QueueRead
        sh.event_fade_cmd(0, true, 0, 0, false)               -- Start, out
        sh.event_step_increment(0)
        step = 1
    end
    if step == 1 then
        sh.event_wait_player_stop()
    elseif step == 2 then
        sh.event_fade_cmd(1, true, 0, 0, false)               -- Wait, out
    elseif step == 3 then
        sh.event_bg_texture_cmd(1, 0, false)                  -- AwaitLoad
    elseif step == 4 then
        sh.event_bg_texture_cmd(2, 0, false)                  -- Draw
        sh.event_fade_cmd(2, false, 0, 0, false)              -- Auto, in
    elseif step == 5 then
        sh.event_bg_texture_cmd(2, 0, false)
        sh.event_msg(false, "map1_s03.hearing_this_the_hunter_armed", 0, 0, 0, false)               -- "Hearing this, the hunter armed with bow..."
    elseif step == 6 then
        sh.event_bg_texture_cmd(2, 0, false)
        sh.event_fade_cmd(2, true, 0, 0, false)               -- Auto, out
    elseif step == 7 then
        sh.event_fade_cmd(0, false, 0, 0, false)              -- Start, in
        -- flag M1S03_LibraryFairyTaleRead set: back to the default branch; not set: step 8
        sh.event_sys_state_branch_on_flag("M1S03_LibraryFairyTaleRead", -1, 8, false)
    elseif step == 8 then
        sh.event_fade_cmd(1, false, 0, 0, false)              -- Wait, in
    elseif step == 9 then
        sh.event_msg(false, "map1_s03.this_is_from_an_old", 0, 0, 0, false)               -- "This is from an old fairy tale."
    else
        sh.player_freeze(false)
        sh.state_set(0)
        sh.flag_set("M1S03_LibraryFairyTaleRead", true)
    end
end

-- index 11: message over a background picture.
function InspectEmptyLocker()
    sh.event_msg_with_bg("TIM/LOCKER1.TIM", 10240, 10240, "map1_s03.nothing_inside") -- 2.5, 2.5
end

-- index 12: inspect the locker in the locker room (animations 85/86, LOCKER1 background).
-- Step 2 stops the player's character in place (chara_field_set "move_speed"; the
-- player movement speed cleared by player_move_speed_clear is a different value).
local function StepSet(v)  -- also resets the state timer and the sub-steps
    sh.event_step_set(0, v)
    sh.state_timer_set(0)
    sh.event_step_set(1, 0)
    sh.sys_timer_set(0)
    sh.event_step_set(2, 0)
end

local function CamPos(x, y, z, warp)
    sh.camera_pos(0, 0, 0, x, y, z, 0, 0, 0, 0, warp)
end

local function CamLook(x, y, z, warp)
    sh.camera_look(0, 0, 0, x, y, z, 0, 0, 0, 0, warp)
end

function InspectLockerRoomLocker()
    local step = sh.event_step_get(0)
    if sh.input_action("skip", 1) and step >= 3 and step < 11 then
        sh.screen_fade_reset_timestep()
        sh.flag_set("M1S03_LockerDoorPulled", true)
        sh.flag_set("M1S03_LockerDoorOpened", true)
        sh.flag_set("M1S03_LockerOpened", true)                                    -- flag: locker opened
        StepSet(13)
        step = 13
    end

    if step == 0 then
        sh.player_freeze(true)
        sh.flag_set("M1S03_LockerInspectStarted", true)
        sh.event_step_increment(0)
        step = 1
    end
    if step == 1 then
        sh.event_wait_player_stop()
        return
    end
    if step == 2 then
        local _, py = sh.player_pos()
        sh.player_set_pos(-243138, py, 75243)                     -- -59.36, 18.37 m
        sh.chara_field_set(0, "move_speed", 0)
        sh.player_rotate(1024)                                    -- 90 deg
        CamPos(-246415, -11059, 80936, true)                      -- (-60.16, -2.7, 19.76)
        CamLook(-238100, -163, 71966, true)                       -- (-58.13, -0.04, 17.57)
        sh.held_item_hide()
        sh.player_weapon_stash()
        sh.event_step_increment(0)
        step = 3
    end
    if step == 3 then
        sh.event_fade_cmd(2, 1, 2, 0, 0)                          -- Auto, out, type 2
        return
    elseif step == 4 then
        sh.event_wait_timer(4096, 0)                              -- 1.0 s
        return
    end
    if step == 5 then
        sh.flag_set("M1S03_LockerDoorPulled", true)
        sh.event_step_increment(0)
        step = 6
    end
    if step == 6 then
        sh.chara_anim_play_to_end(0, "scene_locker_cat_1")
        return
    end
    if step == 7 then
        sh.flag_set("M1S03_LockerDoorOpened", true)
        sh.event_chara_anim(0, 0, "scene_locker_cat_2", false)                      -- SetState
        sh.event_step_increment(0)
        step = 8
    end
    if step == 8 then
        sh.event_wait_timer(8192, 0)                              -- 2.0 s
    elseif step == 9 then
        sh.event_sys_state_branch_on_flag("M1S03_LockerOpened", 10, 9, false)
    elseif step == 10 then
        sh.event_bg_texture_fade_in("TIM/LOCKER1.TIM", 12288, 6144) -- 3.0, 1.5
    elseif step == 11 then
        sh.event_bg_texture_cmd(2, 0, false)                      -- Draw
        sh.event_msg(false, "map1_s03.nothing_inside", 0, 0, 0, false)                   -- "Nothing inside."
    elseif step == 12 then
        sh.event_bg_texture_fade_out(6144, 12288)                 -- 1.5, 3.0
    elseif step == 13 then
        sh.event_chara_anim(4, 0, 0, false)                       -- AnimReset
        sh.flag_set("M1S03_LockerInspectDone", true)
        CamPos(-248340, -16957, 71352, true)                      -- (-60.63, -4.14, 17.42)
        CamLook(-245760, 0, 75776, true)                          -- (-60.0, 0.0, 18.5)
        sh.event_fade_cmd(0, 0, 2, 0, 0)                          -- Start, in, type 2
        sh.event_step_increment(0)
        sh.held_item_show()
        sh.player_weapon_restore()
    else
        sh.player_freeze(false, false)
        sh.state_set(0)
        if sh.screen_fade_finished() then
            sh.event_fade_cmd(0, 0, 0, 0, 0)
        end
    end
end

-- index 13: event at the opened locker (camera, the player turns to the locker, water,
-- splash sound locker_slam, branch on flag M1S03_LockerBodyFellOut). Camera in metres.
function MapEvent_LockerDeadBodyCutscene()
    local step = sh.event_step_get(0)
    if step == 0 then
        sh.player_freeze(true)
        sh.event_step_increment(0)
        step = 1
    end
    if step == 1 then
        sh.flag_set("M1S03_LockerBodySceneStarted", true)
        sh.camera_pos(-59.93, 0.0, 20.18, 0, 0, 0, 0, 0, 0, 0, true)
        sh.camera_look(-62.99, -2.29, 21.37, 0, 0, 0, 0, 0, 0, 0, true)
        local px, _, pz = sh.player_pos()
        sh.player_rotate(sh.math_atan2(-251494 - px, 85196 - pz)) -- -61.4, 20.8
        sh.water_effect_enable()
        sh.chara_slot_anim_flags_clear(0, 2) -- hide the player model
        sh.event_step_increment(0)
        step = 2 -- continues into step 2 in the same tick
    end
    if step == 2 then
        sh.event_wait_timer(2867, false) -- 0.7 s
    elseif step == 3 then
        sh.sfx_flags_play("locker_slam", -246988, 0, 82739, 128, 0) -- (-60.3, 0, 20.2), 0.5
        sh.event_step_increment(0)
        step = 4 -- continues into step 4 in the same tick
    end
    if step == 4 then
        sh.event_sys_state_branch_on_flag("M1S03_LockerBodyFellOut", 5, 4, false)
    elseif step == 5 then
        sh.event_wait_timer(3276, false) -- 0.8 s
    elseif step < 0 or step > 5 then -- default
        sh.player_freeze(false)
        sh.state_set(0)
        sh.flag_set("M1S03_LockerBodySceneDone", true)
        sh.chara_slot_anim_flags_set(0, 2) -- show the player model again
        sh.water_effect_disable()
    end
end

-- index 14: library reserve key pickup with the inspect animation
-- (same pattern as the steel pipe pickup, map2_s00 index 7).
function PickupLibraryReserveKey()
    local step = sh.event_step_get(0)
    if step == 0 then
        sh.player_freeze(true)
        sh.event_inv_item_cmd(0, "LibraryReserveKey", 0, false)                -- QueueLoad, library reserve key
        sh.event_step_increment(0)
        step = 1
    end
    if step == 1 then
        sh.event_wait_player_stop()
    elseif step == 2 then
        sh.chara_anim_play_to_end(0, "pickup_crouch")                      -- bend down
    elseif step == 3 then
        sh.event_inv_item_cmd(1, "LibraryReserveKey", 0, false)                -- AwaitLoad
    elseif step == 4 then
        sh.flag_set("M1S03_PickupLibraryReserveKey", true)
        if sh.pickup_animate("LibraryReserveKey") then
            sh.event_msg(true, "map1_s03.there_is_a_library_reserve", 5, 6, 0, false)            -- take / leave
        end
        if sh.event_step_get(0) == 6 then
            sh.flag_set("M1S03_PickupLibraryReserveKey", false)
        end
    elseif step == 5 then
        sh.event_inv_item_cmd(3, "LibraryReserveKey", 1, false)                -- AddItem
        sh.event_step_increment(0)
        step = 6                                              -- continues into step 6 in the same tick
    end
    if step == 6 then
        sh.chara_anim_play_to_end(0, "pickup_stand_up")                      -- straighten up
    elseif step > 6 then
        sh.player_freeze(false)
        sh.state_set(0)
    end
end

-- index 15: throwing the rubber ball into the courtyard pipe.
-- The ball sound plays at a fixed point (-10.5, 0, -17.5).
function UseRubberBall()
    local step = sh.event_step_get(0)
    if step == 0 then
        sh.player_freeze(true)
        sh.event_bg_texture_cmd(0, "TIM/BALLHOLE.TIM", false) -- QueueRead
        sh.event_step_increment(0)
        step = 1
    end
    if step == 1 then
        sh.event_wait_player_stop()
    elseif step == 2 then
        sh.chara_anim_play_to_end(0, "pickup_crouch")                      -- wind up
    elseif step == 3 then
        sh.event_fade_cmd(2, true, 0, 0, false)               -- Auto, out
    elseif step == 4 then
        sh.sfx_flags_play("rubber_ball_throw", -43008, 0, -71680, 128, 0)    -- sound rubber_ball_throw at (-10.5, 0.0, -17.5)
        sh.flag_set("M1S02_UsedRubberBall", true)
        sh.event_step_increment(0)
        step = 5 -- continues into step 5 in the same tick
    end
    if step == 5 then
        sh.event_bg_texture_cmd(1, 0, false)                  -- AwaitLoad
    elseif step == 6 then
        sh.event_bg_texture_cmd(2, 0, false)                  -- Draw
        sh.event_fade_cmd(2, false, 0, 0, false)              -- Auto, in
    elseif step == 7 then
        sh.event_msg(false, "map1_s03.used_the_rubber_ball", 0, 0, 0, false)               -- "Used the rubber ball."
        sh.event_bg_texture_cmd(2, 0, false)
    elseif step == 8 then
        sh.event_bg_texture_cmd(2, 0, false)
        sh.event_fade_cmd(2, true, 0, 0, false)               -- Auto, out
    elseif step == 9 then
        sh.event_fade_cmd(2, false, 0, 0, false)              -- Auto, in
    elseif step == 10 then
        sh.chara_anim_play_to_end(0, "pickup_stand_up")                      -- straighten up
    elseif step < 0 or step > 10 then -- default
        sh.player_freeze(false)
        sh.state_set(0)
    end
end

-- index 16: UFO1.DMS cutscene (UFO light, "Channeling Stone"): the UFO hum (sound ufo_hum)
-- fades in and out with a wobbling pitch, the glow sprites follow ufo_pos.
-- ufo_hum_clock / ufo_hum_volume / ufo_pos / ufo_msg_audio_idx are map state used only by
-- this scene; the DMS is in file buffer 13.
local UfoDmsBuf = 13

local function UfoGlow()
    sh.map.ufo_glow_spawn("ufo_pos", 2048, 204, 0) -- 0.5, 0.05
end

function Cutscene_UfoChannelingStone()
    local clock = sh.map.get("ufo_hum_clock")
    local quarter_clock = clock // 4
    local sin0 = sh.math_sin(quarter_clock)
    local cos0 = sh.math_cos(clock // 16)
    local pitch = ((sin0 * 2) + cos0 + sh.math_cos(sh.math_sin(quarter_clock * 2))) // 512

    local step = sh.event_step_get(0)
    if step >= 10 then
        local v = sh.map.get("ufo_hum_volume") - (sh.delta_time_get() * 2048) // 4096 -- 0.5
        sh.map.set("ufo_hum_volume", v)
        if sh.map.get("ufo_hum_volume") < 0 then
            sh.map.set("ufo_hum_volume", 0)
        end
        sh.sfx_attributes_update("ufo_hum", 0, ~(sh.map.get("ufo_hum_volume") >> 4), pitch)
    elseif step > 0 then
        local v = sh.map.get("ufo_hum_volume") + (sh.delta_time_get() * 1024) // 4096 -- 0.25
        sh.map.set("ufo_hum_volume", v)
        if sh.map.get("ufo_hum_volume") > 4080 then -- 0.9961
            sh.map.set("ufo_hum_volume", 4080)
        end
        sh.sfx_attributes_update("ufo_hum", 0, ~(sh.map.get("ufo_hum_volume") >> 4), pitch)
    end

    if step == 0 then
        sh.player_freeze(true)
        sh.map.set("ufo_msg_audio_idx", 0)
        sh.load.tim("TIM/ENBAN.TIM", 1, "ufo_glow_tex")
        sh.load.file("ANIM/UFO1.DMS", UfoDmsBuf)
        sh.map.set("cutscene_timer", -1) -- none
        sh.screen_fade_reset_timestep()
        sh.sd_call("ufo_hum")
        sh.sfx_attributes_update("ufo_hum", 0, 255, 0) -- 1.0
        sh.map.set("ufo_hum_volume", 0)
        sh.map.set("ufo_hum_clock", 0)
        sh.event_step_increment(0)
        step = 1
    end
    if step == 1 then
        sh.event.wait_player_stop()
    elseif step == 2 then
        if sh.load.chunks() ~= 0 then
            sh.event_step_increment(0)
        end
    elseif step >= 3 and step <= 5 then
        if step == 3 then
            sh.map.set("cutscene_timer", 0)
            sh.event_fade_cmd(0, true, 3, 0, false) -- Start, type 3
            sh.cutscene.prepare(UfoDmsBuf)
            sh.player.held_item_hide()
            sh.player.weapon_stash()
            sh.light.flashlight_on()
            sh.event_step_increment(0)
            step = 4
        end
        if step == 4 then
            sh.event_fade_cmd(2, false, 0, 0, false) -- Auto
        end
        sh.msg.show_with_audio("map1_s03.what", "ufo_msg_audio_idx", "ufo_msg_voice_cmds[0]")
    elseif step == 6 then
        sh.cutscene.timer_advance("cutscene_timer", 163840, 0, 122880, true, true) -- 40.0, 0 .. 30.0
        UfoGlow()
    elseif step == 7 then
        sh.event_msg(false, "map1_s03.that_light", 0, 0, 0, false)
        UfoGlow()
    elseif step == 8 then
        sh.vec.short_set(0, 0, 0, 0) -- zero offset
        sh.vec.set(0, -56975, -47104, 90562) -- (-13.91, -11.5, 22.11) m
        sh.camera.look_at_bone("player_bone:2", "svec:0", "vec:0") -- Harry's head
        UfoGlow()
        sh.event_wait_timer(10240, false) -- 2.5 s
    elseif step == 9 then
        UfoGlow()
        local dt = sh.delta_time_get()
        sh.state_timer_set(sh.state_timer_get() + dt)
        if sh.state_timer_get() > 16384 then -- 4.0
            sh.map.set("ufo_pos.x", sh.map.get("ufo_pos.x") + (dt * -4915) // 4096) -- -1.2
            sh.map.set("ufo_pos.y", sh.map.get("ufo_pos.y") + (dt * -1638) // 4096) -- -0.4
        end
        if sh.state_timer_get() > 24576 then -- 6.0
            sh.event_step_increment(0)
        end
    elseif step == 10 then
        sh.event_wait_timer(10240, false) -- 2.5 s
    else
        sh.player_freeze(false, false)
        sh.state_set(0)
        sh.event_fade_cmd(0, false, 2, 0, false) -- Start, type 2
        sh.camera_auto_return(true)
        sh.flag_set("M1S03_ChannelingStoneUsed", true)
        sh.event_inv_item_cmd(3, "ChannelingStone", 1, false) -- AddItem
        sh.player.held_item_show()
        sh.player.weapon_restore()
        sh.map.set("cutscene_timer", -1)
        sh.sfx_stop("ufo_hum")
    end

    sh.map.set("ufo_hum_clock", sh.map.get("ufo_hum_clock") + sh.delta_time_get())
    local t = sh.map.get("cutscene_timer")
    if t >= 0 then
        sh.cutscene.chara(0, "HERO", t, UfoDmsBuf)
        sh.cutscene.camera(t, UfoDmsBuf, "camera_target_pos", "camera_target_look")
    end
end
