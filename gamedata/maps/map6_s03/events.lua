-- map6_s03: Sewer to Lakeside Amusement Park
-- Event scripts of this area: the engine runs a handler every tick while its event is active.
-- Fixed-point numbers: 4096 = 1.0 (metres, seconds); angles: 4096 = 360 degrees.

-- Runs one step of a handler's step table: steps[step], or steps.default for any other step.
local function runStep(steps, step, ...)
    return (steps[step] or steps.default)(...)
end

function MapEvent_CommonItemTake()
    local t = {
        [10] = {"HealthDrink",    "M6S03_HealthDrink0"},
        [11] = {"HandgunBullets", "M6S03_HandgunBullets"},
        [12] = {"HealthDrink",    "M6S03_HealthDrink1"},
    }
    local e = t[sh.event_point()] or {"FirstAidKit", 0}
    sh.event_common_item_take(e[1], e[2])
end

function MapEvent_PaperMapTake()
    -- the prompt by its text key (gamedata/text/en/maps/map6_s03.json); 15 before it is the paper map
    sh.event_paper_map_take("AltSewer", "M6S03_PickupMap", "map6_s03.there_is_a_sewer_map")
end

local hatchDescendSteps = {}

hatchDescendSteps[0] = function()
    sh.player_freeze(true)
    sh.screen_fade_reset_timestep()
    sh.event_step_increment(0)
    return hatchDescendSteps[1]()
end

hatchDescendSteps[1] = function()
    sh.event.wait_player_stop()
end

hatchDescendSteps[2] = function()
    -- Warp player.
    sh.npc.set_pos_x(0, -82119) -- -20.0486 m
    sh.npc.set_pos_y(0, -16670) -- -4.07 m
    sh.npc.set_pos_z(0, 101226) -- 24.7134 m
    sh.npc.set_rot_y(0, 2048) -- 180.0 deg
    sh.light.flashlight_on()
    -- Warp camera.
    sh.camera.position(nil, -80527, 696, 107724, 0, 0, 0, 0, true) -- (-19.66, 0.17, 26.3)
    sh.camera.look_at(nil, -83927, -13230, 99819, 0, 0, 0, 0, true) -- (-20.49, -3.23, 24.37)
    sh.event_chara_anim(0, 0, "hatch_drop_down", false)
    sh.event_fade_cmd(0, false, 0, 6144, false) -- 1.5 s
    sh.event_step_increment(0)
    return hatchDescendSteps[3]()
end

hatchDescendSteps[3] = function()
    sh.npc.set_pos_y(0, sh.npc.pos_y(0) + (sh.delta_time_get() * 1638) // 4096) -- 0.4 m/s
    sh.event_wait_timer(15564, false) -- 3.8 s
end

hatchDescendSteps[4] = function()
    sh.event_fade_cmd(2, true, 0, 6144, false)
    sh.npc.set_pos_y(0, sh.npc.pos_y(0) + (sh.delta_time_get() * 1638) // 4096)
end

hatchDescendSteps.default = function()
    sh.player_freeze(false, true)
    sh.state_set(0)
    sh.flag_set("M6S03_HatchDescendCutsceneDone", true)
    sh.game_state_update()
end

-- index 3: climbing down the hatch: warp, anim 88, the player sinks smoothly
-- at 0.4 m/s, fade in from / out to black.
function Cutscene_HatchDescend()
    local s = sh.event_step_get(0)
    if sh.input_action("skip", 1) then
        if s == 3 then
            sh.event.step_set(0, 4)
            s = 4
        end
    end
    runStep(hatchDescendSteps, s)
end

local hatchClimbUpSteps = {}

hatchClimbUpSteps[0] = function()
    sh.player_freeze(true)
    sh.event_fade_cmd(0, true, 0, 0, false)
    sh.flag_set("M6S03_HatchClimbUpStarted", true)
    sh.event_step_increment(0)
    return hatchClimbUpSteps[1]()
end

hatchClimbUpSteps[1] = function()
    sh.event.wait_player_stop()
end

hatchClimbUpSteps[2] = function()
    sh.event_fade_cmd(1, true, 0, 0, false) -- Wait
end

hatchClimbUpSteps[3] = function()
    sh.npc.set_pos_x(0, 368268) -- 89.9092 m
    sh.npc.set_pos_y(0, -300) -- -0.0733 m
    sh.npc.set_pos_z(0, 77363) -- 18.8875 m
    sh.npc.set_rot_y(0, 1024) -- 90.0 deg
    sh.light.flashlight_on()
    sh.camera.position(nil, 364216, -6717, 82575, 0, 0, 0, 0, true) -- (88.92, -1.64, 20.16)
    sh.camera.look_at(nil, 372654, -18227, 74547, 0, 0, 0, 0, true) -- (90.98, -4.45, 18.2)
    sh.event_chara_anim(0, 0, "hatch_climb_up", false)
    sh.event_fade_cmd(0, false, 0, 6144, false)
    sh.event_step_increment(0)
    return hatchClimbUpSteps[4]()
end

hatchClimbUpSteps[4] = function()
    sh.npc.set_pos_y(0, sh.npc.pos_y(0) - (sh.delta_time_get() * 1699) // 4096) -- 0.415 m/s
    sh.event_wait_timer(20480, false) -- 5.0 s
end

hatchClimbUpSteps[5] = function()
    sh.event_fade_cmd(2, true, 0, 6144, false)
    sh.npc.set_pos_y(0, sh.npc.pos_y(0) - (sh.delta_time_get() * 1679) // 4096) -- 0.41 m/s
end

hatchClimbUpSteps.default = function()
    sh.player_freeze(false, true)
    sh.state_set(0)
    sh.flag_set("M6S03_HatchClimbUpCutsceneDone", true)
end

-- index 4: climbing the ladder out of the hatch: warp, anim 87, the player
-- rises at 0.415 m/s, then 0.41 m/s while the screen fades out.
function Cutscene_HatchClimbUp()
    local s = sh.event_step_get(0)
    if sh.input_action("skip", 1) then
        if s == 4 then
            sh.event.step_set(0, 5)
            s = 5
        end
    end
    runStep(hatchClimbUpSteps, s)
end
