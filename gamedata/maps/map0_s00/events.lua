-- map0_s00: Old Silent Hill - intro sequence
-- Event scripts of this area: the engine runs a handler every tick while its event is active.
-- Fixed-point numbers: 4096 = 1.0 (metres, seconds); angles: 4096 = 360 degrees.

function on_boot()
end

-- index 0: 3D sound at the event point (1.2 m up), then a 0.1 s pause.
function MapEvent_PlayEventSfx()
    local step = sh.event_step_get(0)
    if step == 0 then
        sh.player_freeze(true)
        -- the sound sits at the point's x/z, 1.2 m above the ground, pitch 0
        sh.sfx_3d("intro_street_ambient", 128) -- 0.5 = 128
        sh.event_step_increment(0)
        step = 1 -- continues into step 1 in the same tick
    end
    if step == 1 then
        sh.event_step_increment_delayed(409, false) -- 0.1 s
    elseif step > 1 then
        sh.player_freeze(false)
        sh.state_set(0) -- back to gameplay
    end
end

-- index 2: opening cutscene (OPEN.DMS in file buffer 16): Harry walks into the foggy town,
-- "Cheryl. Where could you be?", "It's strange...", "Cheryl."; skippable on steps 3-12.
-- Afterwards Cheryl stands at (-29.5, 128.7) for the next scenes.
local OpenDmsBuf = 16

-- cutscene_timer = MIN(cutscene_timer + dt * rate, max) (Q12_MULT_PRECISE: 64-bit product >> 12).
local function OpeningTimerAdvance(rate, max)
    local t = sh.map.get("cutscene_timer") + (sh.delta_time_get() * rate) // 4096
    if t > max then
        t = max
    end
    sh.map.set("cutscene_timer", t)
    return t
end

function MapEvent_CutsceneOpening()
    local skip = false
    local step = sh.event_step_get(0)
    if sh.input_action("skip", 1) and step >= 3 and step < 13 then
        skip = true
        sh.event.step_reset()
        step = sh.event_step_get(0)
    end

    if step == 0 or step == 1 then
        if step == 0 then
            -- PC: no DMS pose below before the file is loaded.
            sh.map.set("cutscene_timer", -1)
            sh.player_freeze(true)
            sh.load.file("ANIM/OPEN.DMS", OpenDmsBuf)
            sh.cutscene_border(20)
            sh.screen_fade_reset_timestep()
            sh.sys_flags_set(sh.sys_flags_get() | 8) -- SysFlag_CutsceneActive
            sh.sd_play_sfx("opening_ambience", 0, 144)
            sh.event_step_increment(0)
        end
        if sh.load.chunks() ~= 0 then
            sh.event_step_increment(0)
        end
    elseif step == 2 then
        sh.cutscene.prepare(OpenDmsBuf)
        sh.chara_load(0, "Cheryl", 0)
        sh.event_step_increment_after_fade(0, false, 0, 12288, false) -- 3.0 s
        sh.map.set("cutscene_timer", 0)
        sh.event_step_increment(0)
    elseif step == 3 or step == 4 then
        if step == 3 then
            sh.event_chara_anim(0, 0, "walk_forward", false)
            -- PC: on the PS1 the animation update sets these on both of Harry's models; the
            -- engine's update path does not, so the scene sets them (Unlocked | Visible).
            sh.npc.anim_flags_set(0, 3)
            sh.player.extra_anim_flags_set(3)
            sh.event_step_increment(0)
        end
        if OpeningTimerAdvance(40960, 90112) >= 90112 then -- 10.0/s up to 22.0
            sh.event_step_increment(0)
        end
    elseif step == 5 or step == 6 then
        if step == 5 then
            sh.event_chara_anim(0, 0, "scene_courtyard_intro", false)
            sh.event_step_increment(0)
        end
        if OpeningTimerAdvance(32768, 106496) >= 106496 then -- 8.0/s up to 26.0
            sh.event_step_increment(0)
        end
    elseif step == 7 then
        OpeningTimerAdvance(16384, 294912) -- 4.0/s up to 72.0
        sh.event_selection(false, "map0_s00.cheryl_where_could_you_be", 0, 0, 0, false) -- "Cheryl. Where could you be?"
    elseif step == 8 then
        OpeningTimerAdvance(16384, 294912)
        sh.event_step_increment_delayed(3276, false) -- 0.8 s
    elseif step == 9 then
        OpeningTimerAdvance(16384, 294912)
        sh.event_selection(false, "map0_s00.its_strange", 0, 0, 0, false) -- "It's strange..."
    elseif step == 10 then
        if OpeningTimerAdvance(16384, 294912) >= 294912 then
            sh.event_step_increment(0)
        end
    elseif step == 11 then
        sh.event_step_increment_delayed(6144, false) -- 1.5 s
    elseif step == 12 then
        sh.event_selection(false, "map0_s00.cheryl", 0, 0, 0, false) -- "Cheryl."
    elseif step == 13 then
        sh.event_step_increment_delayed(4096, false) -- 1.0 s
    else -- default (a skip lands here too)
        sh.map.set("cutscene_timer", -1)
        if skip then
            sh.player_freeze(false, true)
            sh.state_set(0) -- gameplay
            if sh.screen_fade_finished() then
                sh.event_step_increment_after_fade(0, false, 0, 0, false)
            end
        else
            sh.player_freeze(false, false)
            sh.state_set(0)
        end
        sh.event_step_increment_after_fade(0, false, 2, 0, false)
        sh.camera_auto_return(true)
        sh.chara_process_loads()
        sh.chara_spawn("Cheryl", 0, -120832, 527155, -1280, 1) -- (-29.5, 128.7), -0.3125
    end

    local t = sh.map.get("cutscene_timer")
    if t >= 0 then
        sh.cutscene.chara(0, "HERO", t, OpenDmsBuf)
        sh.cutscene.camera(t, OpenDmsBuf, "camera_target_pos", "camera_target_look")
    end
end
-- index 3/4/5: "Footsteps?": Cheryl's distant
-- footsteps, a 3 s pause, the player turns, selection 20, Cheryl's route (waypoints 53/54).

-- Cheryl's footstep timer: one static shared by all three events.
local cherylFootstepTimer = 0

-- While the player is farther than 16 m from the reference point (-29.5, 128.7), every
-- 0.8 s Cheryl's footstep plays at the player's position shifted 16 m towards that point,
-- so it seems to come from somewhere ahead in the fog.
local function cherylDistantFootstepSfxPlay()
    local px, _, pz = sh.player_pos()
    local dx = -120832 - px -- -29.5
    local dz = 527155 - pz  -- 128.7
    local distQ12 = sh.math_vec2_mag_q6(dx, dz)
    if distQ12 >= 65536 then -- 16.0 m
        cherylFootstepTimer = cherylFootstepTimer + sh.delta_time_get()
        if cherylFootstepTimer > 3276 then -- 0.8 s
            local angle = sh.math_atan2(dx, dz)
            -- The pitch is rolled before the volume; keep this order so the random sequence matches.
            local pitch = sh.rng_int(-16, 15)
            local vol = sh.rng_uint(75, 106)
            -- The footstep sound comes from the character data (gamedata/chara, footstep slot).
            sh.sfx_play_at_pos(sh.chara_sfx("Cheryl", "footstep", "cheryl_footstep"),
                               px + sh.math_sin(angle) * 16,
                               0, -- 0.0
                               pz + sh.math_cos(angle) * 16,
                               vol, pitch)
            cherylFootstepTimer = 0
        end
    end
end

function MapEvent_CutsceneCherylFootsteps0()
    cherylDistantFootstepSfxPlay()
    local step = sh.event_step_get(0)
    if step == 0 then
        sh.player_freeze(true)
        sh.event_step_increment_after_fade(0, true, 2, 0, false)
        sh.event_step_increment(0)
        step = 1 -- continues into step 1 in the same tick
    end
    if step == 1 then
        sh.event_step_increment_delayed(12288, false) -- 3.0 s
    elseif step == 2 then
        sh.event_wait_player_stop()
    elseif step == 3 then
        sh.event_waypoint_set(false, 0, 0, 2048, 0, 0) -- 180 deg, 0, 0
        sh.event_step_increment(0)
        step = 4 -- continues into step 4 in the same tick
    end
    if step == 4 then
        sh.event_waypoint_player("walk_forward", 1, false)
    elseif step == 5 then
        sh.event_selection(false, "map0_s00.footsteps", 0, 0, 0, false) -- "Footsteps?"
    elseif step == 6 then
        sh.event_waypoint_set(true, 0, 0, 2048, -45056, 577536)  -- 180,  -11.0, 141.0
        sh.event_waypoint_set(true, 0, 1, 2048, -47104, 561152)  -- 180,  -11.5, 137.0
        sh.event_waypoint_set(true, 0, 2, 2048, -36864, 544768)  -- 180,  -9.0,  133.0
        sh.event_waypoint_set(true, 0, 3, -1536, -43008, 532480) -- -135, -10.5, 130.0
        sh.event_waypoint_set(true, 0, 4, -1024, -57344, 528384) -- -90,  -14.0, 129.0
        sh.event_waypoint_set(true, 0, 5, -1024, -73728, 530432) -- -90,  -18.0, 129.5
        sh.event_step_increment(0)
        step = 7 -- continues into step 7 in the same tick
    end
    if step == 7 then
        sh.event_waypoint_player("run_forward", 6, false)
    elseif step > 7 then
        sh.state_set(0) -- back to gameplay
    end
end

function MapEvent_CutsceneCherylFootsteps1()
    cherylDistantFootstepSfxPlay()
    local step = sh.event_step_get(0)
    if step == 0 then
        sh.player_freeze(true)
        sh.event_step_increment_after_fade(0, true, 2, 0, false)
        sh.event_step_increment(0)
        step = 1 -- continues into step 1 in the same tick
    end
    if step == 1 then
        sh.event_step_increment_delayed(12288, false) -- 3.0 s
    elseif step == 2 then
        sh.event_wait_player_stop()
    elseif step == 3 then
        sh.event_waypoint_set(false, 0, 0, -1536, 0, 0) -- -135 deg, 0, 0
        sh.event_step_increment(0)
        step = 4 -- continues into step 4 in the same tick
    end
    if step == 4 then
        sh.event_waypoint_player("walk_forward", 1, false)
    elseif step == 5 then
        sh.event_selection(false, "map0_s00.footsteps", 0, 0, 0, false) -- "Footsteps?"
    elseif step == 6 then
        sh.event_waypoint_set(true, 0, 0, 2048, -24576, 552960)  -- 180, -6.0,  135.0
        sh.event_waypoint_set(true, 0, 1, -1024, -28672, 536576) -- -90, -7.0,  131.0
        sh.event_waypoint_set(true, 0, 2, -1024, -45056, 524288) -- -90, -11.0, 128.0
        sh.event_waypoint_set(true, 0, 3, -1024, -71680, 530432) -- -90, -17.5, 129.5
        sh.event_step_increment(0)
        step = 7 -- continues into step 7 in the same tick
    end
    if step == 7 then
        sh.event_waypoint_player("run_forward", 4, false)
    elseif step > 7 then
        sh.state_set(0) -- back to gameplay
    end
end

function MapEvent_CutsceneCherylFootsteps2()
    cherylDistantFootstepSfxPlay()
    local step = sh.event_step_get(0)
    if step == 0 then
        sh.player_freeze(true)
        sh.event_step_increment_after_fade(0, true, 2, 0, false)
        sh.event_step_increment(0)
        step = 1 -- continues into step 1 in the same tick
    end
    if step == 1 then
        sh.event_step_increment_delayed(12288, false) -- 3.0 s
    elseif step == 2 then
        sh.event_wait_player_stop()
    elseif step == 3 then
        sh.event_waypoint_set(false, 0, 0, -1280, 0, 0) -- -112.5 deg, 0, 0
        sh.event_step_increment(0)
        step = 4 -- continues into step 4 in the same tick
    end
    if step == 4 then
        sh.event_waypoint_player("walk_forward", 1, false)
    elseif step == 5 then
        sh.event_selection(false, "map0_s00.footsteps", 0, 0, 0, false) -- "Footsteps?"
    elseif step == 6 then
        sh.event_waypoint_set(true, 0, 0, -1536, 20480, 528384)  -- -135, 5.0,   129.0
        sh.event_waypoint_set(true, 0, 1, -1024, -24576, 524288) -- -90,  -6.0,  128.0
        sh.event_waypoint_set(true, 0, 2, -1024, -71680, 530432) -- -90,  -17.5, 129.5
        sh.event_step_increment(0)
        step = 7 -- continues into step 7 in the same tick
    end
    if step == 7 then
        sh.event_waypoint_player("run_forward", 3, false)
    elseif step > 7 then
        sh.state_set(0) -- back to gameplay
    end
end
-- index 6: Cheryl spotted: the camera looks at Cheryl (slot 1) from (-22, -2.4, 130.1) and
-- pulls back over 5 s while she runs off along her waypoint (slot 1 waypoints) and Harry
-- runs after her; "Cheryl? Is that Cheryl!?", "Where are you going?", "Hey wait...stop!".
-- M0S00_CherylSpottedCutsceneDone at the end. The first switch drives the camera, the second the scene.
local cherylSpottedCameraWarp = false -- an overlay u8 in the original; nothing sets it

local function CherylLookAt(warp)
    sh.camera.look_at_now("pos:1", 0, -4096, 0, 0, 0, 0, 0, warp) -- Cheryl, 1.0 m up
end

function MapEvent_CutsceneCherylSpotted()
    local step = sh.event_step_get(0)
    if step == 0 then
        sh.camera.position_now(nil, -90112, -9830, 532889, 0, 0, 0, 0, true) -- (-22.0, -2.4, 130.1)
        CherylLookAt(true)
    elseif step == 1 or step == 2 then
        sh.event.ramp_reset(0)
        sh.event.ramp_reset(1)
        sh.event.ramp_reset(2)
    elseif step >= 4 and step <= 6 then
        -- The three ramps use separate timers, so their evaluation order does not matter.
        sh.camera.position_now(nil,
                               -90112 - sh.event.ramp_advance(16384, 20480, 0), -- -22.0 - 4.0 over 5 s
                               -9830 - sh.event.ramp_advance(-6144, 20480, 1),  -- -2.4 + 1.5
                               532889 - sh.event.ramp_advance(16384, 20480, 2), -- 130.1 - 4.0
                               0, 0, 0, 0, false)
        CherylLookAt(false)
    elseif step == 3 then
        CherylLookAt(false)
    else
        sh.camera_auto_return(cherylSpottedCameraWarp)
        cherylSpottedCameraWarp = false
    end

    if step == 0 then
        sh.player_freeze(true)
        sh.chara_props_reset(1)
        sh.event_waypoint_set(true, 1, 0, -1536, -143360, 491520) -- -135 deg, (-35.0, 120.0)
        local prevX = sh.npc.pos_x(0)
        local prevZ = sh.npc.pos_z(0)
        sh.map.set("msg_audio_idx", 0)
        -- The chunks around (-40, 127) are set up with Harry moved there for the call.
        sh.npc.set_pos_x(0, -163840)
        sh.npc.set_pos_z(0, 520192)
        sh.load.close_range_chunks()
        sh.npc.set_pos_x(0, prevX)
        sh.npc.set_pos_z(0, prevZ)
        sh.event_step_increment(0)
    elseif step == 1 or step == 2 then
        if step == 1 then
            sh.map.particles_update(0, sh.savegame_get("map_idx"), 0)
            sh.event_step_increment(0)
        end
        sh.msg.play_with_audio("map0_s00.cheryl_is_that_cheryl", "msg_audio_idx", "msg_audio_cmds") -- "Cheryl? Is that Cheryl!?"
    elseif step == 3 then
        sh.entity.set(1, "npc_move_distance", 7372) -- 1.8
        sh.npc.path_walk(1, "Cheryl:run_forward", 1, false)
        sh.event_step_increment_delayed(6144, false) -- 1.5 s
    elseif step == 4 then
        sh.entity.set(1, "npc_move_distance", 7372)
        sh.npc.path_walk(1, "Cheryl:run_forward", 1, false)
    elseif step == 5 or step == 6 then
        if step == 5 then
            sh.npc.set_pos_x(0, -106496) -- (-26.0, 127.0), -135 deg
            sh.npc.set_pos_z(0, 520192)
            sh.npc.set_rot_y(0, -1536)
            sh.event_waypoint_set(true, 0, 0, -1536, -118784, 512000) -- -135 deg, (-29.0, 125.0)
            sh.event_step_increment(0)
        end
        sh.event_waypoint_player("run_forward", 1, false)
    elseif step == 7 then
        if sh.load.chunks_init_done() ~= 0 then
            sh.event_step_increment(0)
        end
    elseif step == 8 then
        sh.msg.play_with_audio("map0_s00.where_are_you_going", "msg_audio_idx", "msg_audio_cmds") -- "Where are you going?"
    elseif step == 9 or step == 10 then
        if step == 9 then
            sh.event_waypoint_set(true, 1, 0, -1280, -147456, 489472) -- -112.5 deg, (-36.0, 119.5)
            sh.event_step_increment(0)
        end
        sh.entity.set(1, "npc_move_distance", 2252) -- 0.55
        sh.npc.path_walk(1, "Cheryl:walk_forward", 1, false)
    elseif step == 11 then
        sh.msg.play_with_audio("map0_s00.hey_wait_stop", "msg_audio_idx", "msg_audio_cmds") -- "Hey wait...stop!"
    else
        sh.event_step_increment_after_fade(0, false, 2, 0, false)
        sh.flag_set("M0S00_CherylSpottedCutsceneDone", true)
        sh.player_freeze(false)
        sh.state_set(0) -- gameplay
        sh.map.chara_anim_reset(1, false)
    end
end
-- index 7: turn the player back after Cheryl:
-- freeze -> waypoint (180, 0, -1) -> 0.3 s -> stop -> "I'd better follow Cheryl..."
-- (msg map0_s00.id_better_follow_cheryl) -> walk animation 54.
function MapEvent_CutsceneCherylRedirect0()
    local step = sh.event_step_get(0)
    if step == 0 then
        sh.player_freeze(true)
        sh.event_waypoint_set(false, 0, 0, 2048, 0, -4096) -- 180 deg, 0, -1
        sh.event_step_increment(0)
        step = 1
    end
    if step == 1 then
        sh.event_step_increment_delayed(1228, false) -- 0.3 s
    elseif step == 2 then
        sh.event_wait_player_stop()
    elseif step == 3 then
        sh.event_selection(false, "map0_s00.id_better_follow_cheryl", 0, 0, 0, false) -- "I'd better follow Cheryl..."
    elseif step == 4 then
        sh.event_waypoint_player("run_forward", 1, false)
    else
        sh.player_freeze(false)
        sh.state_set(0)
    end
end

-- index 8: same, waypoint (90, +1, 0).
function MapEvent_CutsceneCherylRedirect1()
    local step = sh.event_step_get(0)
    if step == 0 then
        sh.player_freeze(true)
        sh.event_waypoint_set(false, 0, 0, 1024, 4096, 0) -- 90 deg, 1, 0
        sh.event_step_increment(0)
        step = 1
    end
    if step == 1 then
        sh.event_step_increment_delayed(1228, false) -- 0.3 s
    elseif step == 2 then
        sh.event_wait_player_stop()
    elseif step == 3 then
        sh.event_selection(false, "map0_s00.id_better_follow_cheryl", 0, 0, 0, false)
    elseif step == 4 then
        sh.event_waypoint_player("run_forward", 1, false)
    else
        sh.player_freeze(false)
        sh.state_set(0)
    end
end

-- index 9: same, waypoint (-90, -1, 0).
function MapEvent_CutsceneCherylRedirect2()
    local step = sh.event_step_get(0)
    if step == 0 then
        sh.player_freeze(true)
        sh.event_waypoint_set(false, 0, 0, -1024, -4096, 0) -- -90 deg, -1, 0
        sh.event_step_increment(0)
        step = 1
    end
    if step == 1 then
        sh.event_step_increment_delayed(1228, false) -- 0.3 s
    elseif step == 2 then
        sh.event_wait_player_stop()
    elseif step == 3 then
        sh.event_selection(false, "map0_s00.id_better_follow_cheryl", 0, 0, 0, false)
    elseif step == 4 then
        sh.event_waypoint_player("run_forward", 1, false)
    else
        sh.player_freeze(false)
        sh.state_set(0)
    end
end

-- index 10: same, diagonal waypoint.
function MapEvent_CutsceneCherylRedirect3()
    local step = sh.event_step_get(0)
    if step == 0 then
        sh.player_freeze(true)
        -- the heading is 0.375 of a turn (135 deg); offsets 0.7, -0.7
        sh.event_waypoint_set(false, 0, 0, 1536, 2867, -2867)
        sh.event_step_increment(0)
        step = 1
    end
    if step == 1 then
        sh.event_step_increment_delayed(1228, false) -- 0.3 s
    elseif step == 2 then
        sh.event_wait_player_stop()
    elseif step == 3 then
        sh.event_selection(false, "map0_s00.id_better_follow_cheryl", 0, 0, 0, false)
    elseif step == 4 then
        sh.event_waypoint_player("run_forward", 1, false)
    else
        sh.player_freeze(false)
        sh.state_set(0)
    end
end

-- index 11: Cheryl runs into the alley: Cheryl warp
-- (slot 1 = npcs[0]), waypoints 53/54, follow camera.
-- Camera offsets are computed as fixed-point integers and only then divided by 4096:
-- camera_pos truncates the metres back to fixed point, so fractional metres here would put
-- the camera one unit off.
local alleyCameraWarp = true -- the first follow-camera frame warps

function MapEvent_CutsceneCherylIntoTheAlley()
    local step = sh.event_step_get(0)
    local px, py, pz = sh.player_pos()
    if step == 0 then
        sh.player_freeze(true)
        local cx, cy, _, _ = sh.chara_slot_transform_get(1) -- Cheryl
        sh.chara_slot_transform_set(1, -253952, cy, pz - 77824, 2048) -- -62, vz - 19, 180
        sh.event_step_increment_after_fade(0, true, 2, 0, false)
        sh.event_step_increment(0)
        step = 1 -- continues into step 1 in the same tick
    end
    if step == 1 then
        sh.event_wait_player_stop()
    elseif step == 2 or step == 3 then
        if step == 2 then -- continues into step 3 in the same tick
            sh.event_waypoint_set(false, 0, 0, 2048, 0, 0)
            sh.camera_pos((px + 983) / 4096, (py - 16629) / 4096, (pz - 20480) / 4096,
                          0, 0, 0, 0, 0, 0, 0, true) -- 0.24, -4.06, -5.0
            sh.camera_look((px - 2949) / 4096, (py - 8642) / 4096, (pz - 6676) / 4096,
                           0, 0, 0, 0, 0, 0, 0, true) -- -0.72, -2.11, -1.63
            sh.event_step_increment(0)
        end
        sh.event_waypoint_player("walk_forward", 1, false)
    elseif step == 4 then
        sh.event_waypoint_set(true, 0, 0, 2048, -253952, pz - 40960) -- -62, vz - 10
        sh.event_step_increment_delayed(4096, false) -- 1.0 s
    elseif step == 5 then
        sh.event_waypoint_player("run_forward", 1, false)
        sh.event_step_increment_delayed(4096, false)
    elseif step == 6 then
        sh.event_waypoint_player("run_forward", 1, false)
        local c = sh.math_cos(sh.player_rot_y_get())
        local temp = math.floor(c * 8192 / 4096) + 16384 -- 2.0 * c + 4.0
        local offsetY = math.floor(c * 2457 / 4096) - 7536 -- 0.6 * c - 1.84
        local offsetZ
        if pz + temp < 532480 then -- 130.0
            offsetZ = pz + temp
        else
            offsetZ = 532480
        end
        sh.camera_pos(-62.0, offsetY / 4096, offsetZ / 4096,
                      0, 0, 0, 0, 0, 0, 0, alleyCameraWarp)
        sh.camera_look(px / 4096, -0.7, (pz - 20480) / 4096,
                       0, 0, 0, 0, 0, 0, 0, alleyCameraWarp) -- -0.7, vz - 5.0
        alleyCameraWarp = false
    else
        sh.player_freeze(false)
        sh.state_set(0)
        sh.event_step_increment_after_fade(0, false, 2, 0, false)
        sh.flag_set("M0S00_CherylRunsIntoAlleySeen", true)
        sh.flag_set("M0S00_AlleyCameraActive", true)
        sh.event_waypoint_set(true, 1, 0, 2048, -253952, 200704) -- -62, 49
    end
end

-- index 12: Cheryl walks off into the fog (alley): camera warp, time slowed down
-- (time runs at half speed in steps 3-5), waypoints 54.
function CutsceneCherylIntoFog()
    local step = sh.event_step_get(0)
    if step == 0 then
        sh.camera_pos(-62.0, -2.24, 117.0, 0, 0, 0, 0, 0, 0, 0, true)
        sh.camera_look(-62.0, -0.7, 104.0, 0, 0, 0, 0, 0, 0, 0, true)
        sh.event_tween_reset(2)
        sh.player_freeze(true)
        sh.event_step_increment_after_fade(0, true, 2, 0, false)
        local px, py, pz = sh.player_pos()
        sh.event_waypoint_set(true, 0, 0, -1024, -247808, pz) -- -90, -60.5
        local cx, cy, _, _ = sh.chara_slot_transform_get(1) -- Cheryl
        sh.chara_slot_transform_set(1, -253952, cy, pz - 36864, 2048) -- -62, vz - 9, 180
        sh.event_step_increment(0)
        step = 1 -- continues into step 1 in the same tick
    end
    if step == 1 then
        sh.event_wait_player_stop()
    elseif step == 2 then
        sh.event_waypoint_player("run_forward", 1, false)
    elseif step == 3 then
        sh.delta_time_set(sh.delta_time_get() // 2) -- half-speed time
        sh.event_waypoint_set(false, 0, 0, -1536, 0, 0) -- -135
        sh.event_step_increment_delayed(3276, false) -- 0.8 s
    elseif step == 4 then
        sh.delta_time_set(sh.delta_time_get() // 2)
        sh.flag_set("M0S00_AlleyCameraActive", true)
        sh.event_waypoint_player("run_forward", 1, false)
    elseif step == 5 then
        sh.delta_time_set(sh.delta_time_get() // 2)
        sh.event_step_increment_delayed(4096, false) -- 1.0 s
    else
        sh.event_step_increment_after_fade(0, false, 2, 0, false)
        sh.flag_set("M0S00_CherylRunsIntoAlleySeen", true)
        sh.event_waypoint_set(true, 1, 0, 2048, -253952, 200704)
        sh.player_freeze(false)
        sh.state_set(0)
    end
end
-- index 13: the alley gets darker: "That's strange, it's getting darker.", Harry puts his
-- weapon away, the darkness rises over 0.5 s, "......better than nothing, I guess." (M0S00_AlleyGetsDarkerCutsceneDone),
-- and three Grey Children appear. The flare's flame picture is loaded at the start; the Grey
-- Children start loading as soon as the world chunks are in (alley_enemies_loading).
local AlleyEnemy = "GreyChild" -- the NTSC build; the Japanese one puts Mumblers here

local function AlleyEnemiesLoad()
    sh.npc.load(1, AlleyEnemy, "npc_bone:0", 0, nil, nil)
end

function MapEven_CutsceneAlleyGetsDarker()
    local step = sh.event_step_get(0)
    if step == 0 then
        sh.player_freeze(true)

        sh.map.set("alley_darkness", 0)
        sh.map.set("alley_enemies_loading", 0)

        sh.load.flame_texture()
        sh.event_step_increment(0)
        step = 1 -- continues into step 1 in the same tick
    end

    if step == 1 then
        sh.event_step_increment_after_fade(2, true, 2, 0, false)
    elseif step == 2 then
        sh.event_wait_player_stop()
    elseif step == 3 or step == 4 then
        if step == 3 then
            sh.event_step_increment(0) -- continues into step 4 in the same tick
        end
        sh.event_selection(false, "map0_s00.thats_strange_its_getting", 0, 0, 0, false) -- "That's strange, it's getting darker."
    elseif step == 5 then
        sh.event.branch_on_flag("M0S00_AlleyFullyDark", 6, 5, false)
    elseif step == 6 then
        -- PC: the PS1 code below the break of this case is unreachable, so the load also starts
        -- here when the world chunks are not in yet (the original only waits for the flag).
        if sh.map.get("alley_enemies_loading") ~= 0 then
            sh.event_step_increment(0)
        else
            AlleyEnemiesLoad()
            sh.map.set("alley_enemies_loading", 1)
        end
    elseif step == 7 or step == 8 then
        if step == 7 then
            sh.map.player_weapon_unequip()
            sh.event_step_increment(0) -- continues into step 8 in the same tick
        end
        sh.event_chara_anim(1, 0, 0, false) -- wait for the end of the animation
        -- PC: the step also ends after 2 s when the put-away animation never reports its end.
        sh.event.set_step_timer(sh.event.step_timer() + sh.game.delta_time_raw())
        if sh.event.step_timer() > 8192 then -- 2.0 s
            sh.event_step_increment(0)
        end
    elseif step == 9 or step == 10 then
        if step == 9 then
            -- The effects keep reading alley_darkness (32-bit values) up to 0.5.
            sh.map_effects_update(10, 10, 5, "alley_darkness", 0, 2048)
            sh.event_step_increment(0) -- continues into step 10 in the same tick
        end
        sh.map.set("alley_darkness", sh.map.get("alley_darkness") + sh.game.delta_time())
        if sh.map.get("alley_darkness") > 2048 then -- 0.5
            sh.event_step_increment(0)
        end
    elseif step == 11 then
        sh.flag_set("M0S00_AlleyGetsDarkerCutsceneDone", true)
        sh.event_selection(false, "map0_s00.better_than_nothing_i_guess", 0, 0, 0, false) -- "......better than nothing, I guess."
    else
        sh.event_step_increment_after_fade(0, false, 2, 0, false)
        sh.player_freeze(false, true)
        sh.state_set(0) -- gameplay

        sh.npc.process_loads()
        sh.npc.spawn(AlleyEnemy, 0, -1032192, 913408, 2048, 5) -- (-252.0, 223.0), 180 deg
        sh.npc.spawn(AlleyEnemy, 1, -1040384, 905216, 1536, 5) -- (-254.0, 221.0), 135 deg
        sh.npc.spawn(AlleyEnemy, 2, -1060864, 950272, 0, 5)    -- (-259.0, 232.0), 0 deg
    end

    if sh.map.get("alley_enemies_loading") == 0 then
        if sh.load.chunks() ~= 0 then
            sh.map.set("alley_enemies_loading", sh.map.get("alley_enemies_loading") + 1)
            AlleyEnemiesLoad()
        end
    end
end
-- index 14: "A dead end? What the hell!?" (msg map0_s00.a_dead_end_what_the): freeze -> stop -> message -> unfreeze.
function MapEvent_DeadEndMessage()
    local step = sh.event_step_get(0)
    if step == 0 then
        sh.player_freeze(true)
        sh.event_step_increment(0)
        step = 1 -- continues into step 1 in the same tick
    end
    if step == 1 then
        sh.event_wait_player_stop()
    elseif step == 2 then
        sh.event_selection(false, "map0_s00.a_dead_end_what_the", 0, 0, 0, false) -- "A dead end? What the hell!?"
    else
        sh.player_freeze(false)
        sh.state_set(0)
    end
end

-- index 15: the alley nightmare: the alley's end becomes the THRF908 chunk, Harry walks up to
-- the body hanging on the fence, "What is this?" with the camera at Harry's head looking at the
-- body, then the Grey Children attack. The first switch drives the camera
-- (nightmare_camera_stage, never reset by the scene), the second the scene.
local HarryBoneHead = 2

function MapEvent_CutsceneAlleyNightmare()
    local step = sh.event_step_get(0)
    if step == 3 then
        if sh.map.get("nightmare_camera_stage") == 0 then
            sh.camera.position_now(nil, -1042964, -3153, 899317, 0, 0, 0, 0, true) -- (-254.63, -0.77, 219.56)
            sh.camera.look_at_now("pos:0", 0, -2457, 0, 0, 0, 0, 0, true)          -- Harry, 0.6 m up
            sh.map.set("nightmare_camera_stage", sh.map.get("nightmare_camera_stage") + 1)
        end
        sh.camera.look_at_now("pos:0", 0, -2457, 0, 0, 0, 0, 159, false) -- y turn speed limit 0.039
    elseif step == 4 then
        if sh.map.get("nightmare_camera_stage") == 1 then
            sh.npc.anim_flags_clear(0, 2) -- Harry invisible
            sh.camera.position_now(nil, -1027317, -1310, 895344, 0, 0, 0, 0, true) -- (-250.81, -0.32, 218.59)
            sh.camera.look_at_now(nil, -1012244, -2293, 888995, 0, 0, 0, 0, true)  -- (-247.13, -0.56, 217.04)
            sh.npc.set_pos_x(0, -1028587) -- (-251.12, 0.0, 218.56)
            sh.npc.set_pos_y(0, 0)
            sh.npc.set_pos_z(0, 895221)
            sh.map.set("nightmare_camera_stage", sh.map.get("nightmare_camera_stage") + 1)
        else
            -- The camera sits 38 units above Harry's head bone, in world space (the bone's
            -- matrix applied to the offset; geometry units, 16 = 1/256 m).
            local x, y, z = sh.entity.bone_point(0, HarryBoneHead, 0, -38, 0)
            sh.camera.position_now(nil, x * 16, y * 16, z * 16, 0, 12288, 0, 0, false) -- y acceleration 3.0
            sh.camera.look_at_now(nil, -1013473, -4218, 890224, 0, 0, 0, 0, false)    -- (-247.43, -1.0298, 217.34)
        end
    elseif step > 4 then
        sh.npc.anim_flags_set(0, 2) -- Harry visible again
        sh.camera.auto_return(true)
    end

    if step == 0 then
        sh.player_freeze(true)

        sh.world.chunk_set(sh.file_number("BG/THRF908.IPD"), -7, 6)
        sh.sound.stop("alley_nightmare_sfx")
        sh.sd_play_sfx("darkness_hum", 0, 208)
        sh.event_waypoint_set(true, 0, 0, 2048, -1032192, 897024) -- 180 deg, (-252.0, 219.0)
        sh.event_waypoint_set(true, 0, 1, 1280, -1028096, 894976) -- 112.5 deg, (-251.0, 218.5)

        sh.map.set("nightmare_msg_audio_idx", 0)

        sh.event_step_increment(0)
        step = 1 -- continues into step 1 in the same tick
    end

    if step == 1 then
        sh.event_step_increment_after_fade(2, true, 2, 0, false)
    elseif step == 2 then
        sh.event_wait_player_stop()
    elseif step == 3 then
        sh.event_waypoint_player("walk_forward", 2, false)
    elseif step == 4 then
        sh.msg.play_with_audio("map0_s00.what_is_this", "nightmare_msg_audio_idx", "nightmare_voice_cmds") -- "What is this?"
    elseif step > 4 then
        sh.player_freeze(false)
        sh.state_set(0) -- gameplay
        sh.event_step_increment_after_fade(0, false, 2, 0, false)

        -- The Grey Children turn aggressive.
        local enemy = sh.chara_id(AlleyEnemy)
        for slot = 1, 32 do -- every NPC slot
            if sh.npc.chara_id(slot) == enemy then
                sh.npc.set_control_state(slot, 0)
                sh.npc.set_state_step(slot, 6)
            end
        end
    end
end
-- index 16: reset after the player's death: stop three ambient loops, pad rumble, back to
-- gameplay, player y = 0. All sounds are deliberately not stopped here: that would cut
-- off the death scream.
function MapEvent_AfterDeathReset()
    sh.sfx_stop("darkness_hum")
    sh.sfx_stop("opening_ambience")
    sh.sfx_stop("weather_particles_loop")
    sh.pad_effect(4, 128) -- pad rumble preset 4 at the default level
    sh.state_set(0)       -- back to gameplay
    local px, _, pz = sh.player_pos()
    sh.player_set_pos(px, 0, pz) -- y = 0, x/z kept
end
