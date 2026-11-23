-- map1_s06: School - 1F and basement after the boss
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

-- index 3: no action.
function MapEvent_Nop()
end

-- Fixed-point world units -> metres for camera_pos/look.
local function M(q) return q / 4096 end

-- Setting a step also clears the state timer, the sub-steps and the step timer
-- (sh.event_step_set alone writes only the step).
local function StepSet0(v)
    sh.event_step_set(0, v)
    sh.state_timer_set(0)
    sh.event_step_set(1, 0)
    sh.sys_timer_set(0)
    sh.event_step_set(2, 0)
end

local function WarpPlayer(x, z, rotY)
    local _, py = sh.player_pos()
    sh.player_set_pos(x, py, z)
    sh.player_rotate(rotY)
end

-- index 4: arrival in the basement (voiced lines 15/17, camera cuts, flashlight). The voice
-- index is a Lua local (only this handler reads it); the hum volume is read by the map's
-- sound update, so it is the map state variable basement_hum_volume (sh.map.get/set).
local s_audioIdx = 0

local function SchoolBasementCamera0()
    sh.camera_pos(M(92037), M(-14049), M(-53002), 0, 0, 0, 0, 0, 0, 0, true)  -- 22.47, -3.43, -12.94 m
    sh.camera_look(M(88432), M(-6225), M(-66928), 0, 0, 0, 0, 0, 0, 0, true)  -- 21.59, -1.52, -16.34 m
end

local function SchoolBasementCamera1()
    sh.camera_pos(M(90562), M(-21217), M(-83763), 0, 0, 0, 0, 0, 0, 0, true)  -- 22.11, -5.18, -20.45 m
    sh.camera_look(M(86999), M(-9830), M(-72540), 0, 0, 0, 0, 0, 0, 0, true)  -- 21.24, -2.4, -17.71 m
end

function Cutscene_OldTownBasement()
    local step = sh.event_step_get(0)
    if sh.input_action("skip", 1) and step >= 3 and step < 13 then
        StepSet0(14)
        step = 14
    end

    if step == 0 or step == 1 or step == 2 then
        if step == 0 then
            sh.player_freeze(true)
            sh.map_effects_init(19, 4)
            sh.cutscene_border(20)                     -- force the cutscene borders on
            s_audioIdx = 0
            WarpPlayer(88268, -59801, 2048)            -- 21.55, -14.6, 180 deg
            sh.chara_slot_anim_flags_clear(0, 2)
            SchoolBasementCamera0()
            sh.flashlight(false)
            sh.event_fade_cmd(0, 0, 0, 4096, 0) -- Start, 1.0
            sh.event_step_increment(0)
            step = 1
        end
        if step == 1 then
            if sh.ambient_sfx() then
                return                                 -- wait while the ambient sounds load
            end
            sh.event_step_increment(0)
        end
        sh.event_fade_cmd(1, 0, 0, 0, 0)       -- Wait
    elseif step == 3 then
        s_audioIdx = sh.event_msg_audio("map1_s06.msg15", s_audioIdx, "basement_voice_cmds")
    elseif step == 4 or step == 5 then
        if step == 4 then
            sh.flag_set("M1S06_BasementHumStarted", true)
            sh.event_step_increment(0)
        end
        sh.event_wait_timer(6144, 0)               -- 1.5 s
        local hum = sh.map.get("basement_hum_volume")
        sh.map.set("basement_hum_volume", hum + (sh.delta_time_get() * 409) // 4096) -- dt * 0.1
    elseif step == 6 then
        s_audioIdx = sh.event_msg_audio("map1_s06.who_in_the_hell_was", s_audioIdx, "basement_voice_cmds")
    elseif step == 7 or step == 8 then
        if step == 7 then
            WarpPlayer(87859, -62504, 2048)            -- 21.45, -15.26
            sh.chara_slot_anim_flags_set(0, 2)
            SchoolBasementCamera1()
            sh.event_step_increment(0)
        end
        sh.event_wait_timer(4096, 0)
    elseif step == 9 or step == 10 then
        if step == 9 then
            sh.flashlight(true)
            sh.event_step_increment(0)
        end
        sh.event_wait_timer(4096, 0)
    elseif step == 11 then
        sh.event_msg(false, "map1_s06.where_am_i", 0, 0, 0, false)
    elseif step == 12 then
        sh.event_wait_timer(8192, 0)
    elseif step == 13 then
        sh.event_step_reset()
    elseif step == 14 then
        sh.event_fade_cmd(2, 1, 0, 0, 0)        -- Auto, out
    elseif step == 15 then
        sh.sd_call("stop_xa_audio")
        sh.event_fade_cmd(0, 0, 0, 0, 0)
        WarpPlayer(87859, -62504, 2048)
        SchoolBasementCamera1()
        sh.chara_slot_anim_flags_set(0, 2)
        sh.flashlight(true)
        sh.flag_set("M1S06_BasementHumStarted", true)
        sh.map.set("basement_hum_volume", 614)        -- 0.15
        sh.event_step_increment(0)
    else
        sh.flag_set("M1S06_WokeUpAfterBoss", true)
        sh.map_effects_init(7, 4)
        sh.camera_auto_return(false)
        sh.event_fade_cmd(0, 0, 2, 0, 0)
        sh.player_freeze(false)
        sh.state_set(0)
        sh.game_state_update()
    end
end

-- index 5: K. Gordon key pickup with the inspect animation.
function PickupKGordonKey()
    local step = sh.event_step_get(0)
    if step == 0 then
        sh.player_freeze(true)
        sh.event_inv_item_cmd(0, "KGordonKey", 0, false) -- queue the item model load
        sh.event_step_increment(0)
        step = 1
    end
    if step == 1 then
        sh.event_wait_player_stop()
    elseif step == 2 then
        sh.chara_anim_play_to_end(0, "pickup_crouch")
    elseif step == 3 then
        sh.event_inv_item_cmd(1, "KGordonKey", 0, false) -- wait for the model load
    elseif step == 4 then
        sh.flag_set("M1S06_PickupKGordonKey", true)                         -- flag: pickup K. Gordon key
        if sh.pickup_animate("KGordonKey") then
            sh.event_msg(true, "map1_s06.there_is_a_k_gordon", 5, 6, 0, false)
        end
    elseif step == 5 then
        sh.event_inv_item_cmd(3, "KGordonKey", 1, false) -- add the item
        StepSet0(7)
    elseif step == 6 or step == 7 then
        if step == 6 then
            sh.flag_set("M1S06_PickupKGordonKey", false)
            sh.event_step_increment(0)
        end
        sh.chara_anim_play_to_end(0, "pickup_stand_up")
    else
        sh.player_freeze(false)
        sh.state_set(0)
    end
end

-- index 6: church bell: line 22, the town map scrolling (BellMapDraw) and the
-- church mark (926) fading in. The slide/saved map index/mark alpha/skip-fade state and the
-- static fade accumulators are read only by this handler, so they are Lua locals.
local TIMESTEP_30_FPS = 136 -- 1.0 / 30 s
local s_bellSlide = 0
local s_bellPaperMapIdx = 0  -- saved paper map index
local s_bellMarkAlpha = 0
local s_bellSkipFade = 0
local s_bellSlideAccum = 0   -- slide accumulator, kept across ticks
local s_bellFadeAccum = 0    -- fade accumulator, kept across ticks

-- The scrolling town map: its outline, Harry's mark and the map's markings, scrolled by `scroll`,
-- at y 120 and scale 0.5.
local function BellMapDraw(scroll)
    sh.map_screen_vertex_draw(scroll, 120, 2048)
    sh.map_screen_player_mark_draw(1, scroll, 120, 2048)
    sh.map_screen_marking_draw(1, 1, 0, 0, scroll, 120, 2048)
end

function Cutscene_ChurchBellMap()
    local SKIP = 14
    if sh.input_action("skip", 1) then
        local step = sh.event_step_get(0)
        if step > 0 and step < 6 then
            StepSet0(SKIP)
        end
        step = sh.event_step_get(0)
        if s_bellSkipFade == 0 and step > 6 and step < 12 then
            s_bellSkipFade = 1
            sh.event_fade_cmd(0, 1, 0, 0, 0)                -- Start, out
        end
    end
    if s_bellSkipFade ~= 0 and sh.screen_fade_finished() then
        s_bellSkipFade = 0
        StepSet0(13)
    end

    local step = sh.event_step_get(0)
    if step == 0 or step == 1 then
        if step == 0 then
            sh.player_freeze(true)
            sh.screen_fade_reset_timestep()
            sh.cutscene_border(20)
            WarpPlayer(447283, 176947, 1864)                -- 109.2, 43.2 m, 163.8 deg
            s_bellPaperMapIdx = sh.savegame_get("paper_map_idx") & 0xFF
            sh.savegame_set("paper_map_idx", "OldTown")
            s_bellSkipFade = 0
            sh.camera_pos(M(436551), M(-14827), M(185262), 0, 0, 0, 0, 0, 0, 0, true) -- 106.5799, -3.6199, 45.23 m
            sh.camera_look(M(447242), M(-5611), M(176947), 0, 0, 0, 0, 0, 0, 0, true) -- 109.19, -1.3699, 43.2 m
            sh.event_step_increment(0)
        end
        sh.event_fade_cmd(2, 0, 0, 4096, 0)                 -- Auto, in, 1.0
    elseif step == 2 then
        sh.event_wait_timer(4096, 0)
    elseif step == 3 then
        sh.event_msg(false, "map1_s06.i_hear_a", 0, 0, 0, false)             -- "I hear a church bell."
    elseif step == 4 or step == 5 then
        if step == 4 then
            sh.fs_seek(sh.file_number("TIM/MP_0TOWN.TIM") + sh.map.get("paper_map_tex_offset")) -- MP_0TOWN.TIM + offset
            sh.event_step_increment(0)
        end
        sh.event_fade_cmd(2, 1, 0, 0, 0)                    -- Auto, out
    elseif step == 6 or step == 7 then
        if step == 6 then
            sh.paper_map_cmd(0, "OldTown")                 -- load the paper map
            s_bellSlide = 0
            s_bellMarkAlpha = 0
            sh.event_step_increment(0)
        end
        BellMapDraw(0)
        sh.event_fade_cmd(2, 0, 0, 0, 0)                    -- Auto, in
    elseif step == 8 then
        BellMapDraw(0)
        sh.event_wait_timer(2457, 0)                        -- 0.6 s
    elseif step == 9 then
        s_bellSlideAccum = s_bellSlideAccum + (4096 * sh.delta_time_get()) // TIMESTEP_30_FPS
        local slideStep = s_bellSlideAccum >> 12
        s_bellSlideAccum = s_bellSlideAccum - (slideStep << 12)
        s_bellSlide = math.min(s_bellSlide + slideStep, 122)
        BellMapDraw(s_bellSlide)
        if s_bellSlide == 122 then
            sh.event_step_increment(0)
        end
    elseif step == 10 then
        BellMapDraw(122)
        sh.event_wait_timer(2457, 0)
    elseif step == 11 then
        BellMapDraw(122)
        sh.map_screen_marking_draw(2, 1, "MapMark_OldTown_BalkanChurchCircle", s_bellMarkAlpha, 122, 120, 2048)
        s_bellFadeAccum = s_bellFadeAccum + (4096 * sh.delta_time_get()) // TIMESTEP_30_FPS
        local fadeStep = s_bellFadeAccum >> 12
        s_bellFadeAccum = s_bellFadeAccum - (fadeStep << 12)
        s_bellMarkAlpha = (s_bellMarkAlpha + fadeStep) & 0xFF
        if s_bellMarkAlpha >= 0x80 then
            s_bellMarkAlpha = 0x80
            if sh.input_action("enter", 1) or sh.input_action("cancel", 1) then
                sh.event_step_increment(0)
            end
            if sh.event_step_get(0) == 11 then
                sh.event_wait_timer(6144, 0)                -- 1.5 s
            end
        end
    elseif step == 12 then
        BellMapDraw(122)
        sh.map_screen_marking_draw(2, 1, "MapMark_OldTown_BalkanChurchCircle", 128, 122, 120, 2048)
        sh.event_fade_cmd(2, 1, 0, 0, 0)
    elseif step == 13 or step == SKIP then
        if step == 13 then
            sh.paper_map_cmd(2, "OtherPlaces")              -- unload the paper map
            sh.event_step_increment(0)
        end
        sh.event_fade_cmd(2, 1, 0, 0, 0)
    else
        sh.event_fade_cmd(0, 0, 2, 0, 0)
        sh.camera_auto_return(true)
        sh.event_fade_cmd(0, 0, 0, 0, 0)
        sh.savegame_set("paper_map_idx", s_bellPaperMapIdx)
        sh.player_freeze(false)
        sh.state_set(0)
        sh.flag_set("M1S06_ChurchMarkedOnMap", true)                              -- flag: church marked on map
        sh.flag_set("MapMark_OldTown_BalkanChurchCircle", true)                              -- map mark: Old Town Balkan church circle
    end
end

-- index 7: reading the note by the book (dimmed background 7/16 = 112, picture RSCBOOK.TIM = 1938).
function MapEvent_BookNoteRead()
    sh.background_gamma(112)

    if sh.flag_get("M1S06_PickupKGordonKey") then -- flag: pickup K. Gordon key
        sh.event_msg_with_bg("TIM/RSCBOOK.TIM", 0, 0, "map1_s06.list_of_teachers")
        sh.flag_set("MapMark_OldTown_KGordonDotAndSignOnly", true) -- map mark: Old Town K. Gordon dot and sign only
    else
        sh.event_msg_with_bg("TIM/RSCBOOK.TIM", 0, 0, "map1_s06.moore_ranaldo_gordon_this_must")
    end
end

-- index 8: paper map pickup.
function MapEvent_PaperMapTake()
    sh.event_paper_map_take("FogSchool1F", "M1S00_PickupMap", "map1_s06.there_is_a_school_map")
end
