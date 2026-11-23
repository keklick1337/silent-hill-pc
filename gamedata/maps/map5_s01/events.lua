-- map5_s01: Resort Area
-- Event scripts of this area: the engine runs a handler every tick while its event is active.
-- Fixed-point numbers: 4096 = 1.0 (metres, seconds); angles: 4096 = 360 degrees.

-- Runs one step of a handler's step table: steps[step], or steps.default for any other step.
local function runStep(steps, step, ...)
    return (steps[step] or steps.default)(...)
end

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

-- index 2: common pickups, chosen by the event's point of interest.
function MapEvent_CommonItemTake()
    local poi = sh.event_point()
    local t = {
        [40] = {"RifleShells","M5S01_RifleShells0"},
        [41] = {"HandgunBullets","M5S01_HandgunBullets"},
        [42] = {"FirstAidKit","M5S01_FirstAidKit"},
        [43] = {"HealthDrink","M5S01_HealthDrink"},
        [44] = {"RifleShells","M5S01_RifleShells1"},
    }
    local e = t[poi]
    if e == nil then
        -- any other point gives a first aid kit and sets no flag
        e = {"FirstAidKit", 0}
    end
    sh.event_common_item_take(e[1], e[2])
end

-- Integer division that truncates toward zero.
local function idiv(a, b)
    local q = a // b
    if q < 0 and q * b ~= a then q = q + 1 end
    return q
end

-- index 3: door code panel (DORPANEL.TIM, cursor).
-- The entered code (four digits) and the cursor position are used only by this handler,
-- so they are kept local; the mouse cursor override of the puzzle reads them through
-- the vec:14/vec:15 scratch.
local s_panelCode = {10, 10, 10, 10}
local s_panelCursorX = 0
local s_panelCursorY = 0

local doorCodePanelSteps = {}

doorCodePanelSteps[0] = function()
    sh.player_freeze(true)
    sh.event_fade_cmd(0, true, 0, 0, false) -- Start
    s_panelCursorY = 0
    s_panelCursorX = 0
    for i = 1, 4 do
        s_panelCode[i] = 10
    end
    sh.event_step_increment(0)
    return doorCodePanelSteps[1]()
end

doorCodePanelSteps[1] = function()
    sh.event.bg_texture(7, "TIM/DORPANEL.TIM", false) -- Auto, DORPANEL.TIM
end

doorCodePanelSteps[2] = function()
    sh.event_fade_cmd(1, true, 0, 0, false) -- Wait
end

doorCodePanelSteps[3] = function()
    sh.event_fade_cmd(2, false, 0, 0, false) -- Auto
    sh.screen.bg_draw("item_inspection_img")
end

doorCodePanelSteps[4] = function()
    sh.screen.bg_draw("item_inspection_img")

    sh.vec.set(14, s_panelCursorX, 0, 0)
    sh.vec.set(15, s_panelCursorY, 0, 0)
    sh.screen.puzzle_cursor("vec:14", "vec:15")
    s_panelCursorX = sh.vec.get(14)
    s_panelCursorY = sh.vec.get(15)
    -- Stick moves the cursor at a frame-rate independent speed (30 fps base, 1/30 s = 136).
    local dt = sh.delta_time_get()
    s_panelCursorX = s_panelCursorX + idiv(idiv(sh.input_axis(1, 0) * 16384, 75) * dt, 136)
    s_panelCursorX = math.min(math.max(s_panelCursorX, -655360), 655360) -- -160.0, 160.0
    s_panelCursorY = s_panelCursorY + idiv(idiv(sh.input_axis(1, 1) * 16384, 75) * dt, 136)
    s_panelCursorY = math.min(math.max(s_panelCursorY, -491520), 491520) -- -120.0, 120.0

    sh.game.timer_update()
    local cx = ((s_panelCursorX // 4096 + 8 + 0x8000) & 0xFFFF) - 0x8000 -- whole pixels + 8, as a signed 16-bit value
    sh.screen.cursor_draw(cx, s_panelCursorY // 4096 + 8, 8, 8, 0, 64, 32, 32, 128, 192, 0, 12)

    if sh.input_action("cancel", 1) then
        sh.event.step_set(0, 7)
        return
    end

    if sh.input_action("enter", 1) then
        local fx = s_panelCursorX // 4096
        local fy = s_panelCursorY // 4096
        for i = 0, 11 do -- 12 panel buttons
            local f0 = sh.map.get("code_panel_buttons.x", i)
            local f1 = sh.map.get("code_panel_buttons.y", i)
            if not ((f0 - 160) > fx or (f0 - 114) < fx) and
               not ((f1 - 120) > fy or (f1 - 74) < fy) then
                sh.sound.play("keypad_beep", 0, 128) -- sound keypad_beep, 0.5
                if i < 11 then
                    for j = 1, 3 do
                        s_panelCode[j] = s_panelCode[j + 1]
                    end
                    s_panelCode[4] = i
                    break
                end
                local j = 0
                while j < 4 do
                    if sh.map.get("code_panel_solution", j) ~= s_panelCode[j + 1] then
                        break
                    end
                    j = j + 1
                end
                if j == 4 then
                    sh.flag_set("M5S01_DoorCodePanelSolved", true)
                    sh.event_step_increment(0)
                else
                    for k = 1, 4 do
                        s_panelCode[k] = 10
                    end
                end
                break
            end
        end
    end
end

doorCodePanelSteps[5] = function()
    sh.event_wait_timer(2457, false) -- 0.6 s
    sh.screen.bg_draw("item_inspection_img")
end

doorCodePanelSteps[6] = function()
    sh.sound.play("keypad_error", 0, 128)
    sh.event_step_increment(0)
    return doorCodePanelSteps[7]()
end

doorCodePanelSteps[7] = function()
    sh.screen.bg_draw("item_inspection_img")
    sh.event_fade_cmd(2, true, 0, 0, false)
end

doorCodePanelSteps.default = function()
    sh.player_freeze(false, false)
    sh.state_set(0)
    sh.event_fade_cmd(0, false, 0, 0, false)
    sh.sound.radio_npc_clear()
end

function Puzzle_DoorCodePanel()
    sh.sound.radio_stop()

    local s = sh.event_step_get(0)
    runStep(doorCodePanelSteps, s)
end

-- index 4: paper map pickup.
function MapEvent_PaperMapTake()
    sh.event_paper_map_take("ResortTown", "M5S01_PickupMap", "map5_s01.there_is_a_resort_area")
end

-- Combination lock close-up (Puzzle_DialCode): four number reels (0..7) between two end caps,
-- the six models of ITEM/FOOK.TMD in item-view slots 0..5, over a round dial face drawn in the UI
-- table. Up/down picks a reel, left/right turns it; the code reads reel 0 as units, reel 3 as
-- thousands.
-- The reels, the shown reels and the last settled code are kept in the session: the original
-- keeps them outside the map data, so a later visit finds the lock as it was left.
local LOCK = {
    reels = 4,
    slots = 6,
    z = -8320,                       -- Q8(-32.5): every slot's distance in front of the camera
    cap_y = {[0] = -52, [5] = -20},  -- Q8(-0.204), Q8(-0.08): end caps
    reel_y0 = 398,                   -- Q8(1.555): reel 1 (reel_y_step lower per slot)
    reel_y_step = 106,               -- Q8(0.415)
    light_z = 20000,                 -- the light sits this far behind each model
    scale = 8192,                    -- 2.0
    tilt = 1024,                     -- 90 deg about x, every frame
    cap_yaw = 3328,                  -- 292.5 deg
    reel_yaw = 2816,                 -- 247.5 deg; plus 45 deg per number
    fade_frames = 32,                -- fade-in, 30 fps frames
    turn_frames = 16,                -- one number of a reel turning, 30 fps frames
    dial_inner = 90, dial_outer = 180, dial_sectors = 16,
    arrow_step = 20,
}

local lock_state, lock_counter, lock_cursor = 0, 0, 0
local lock_fade_accum, lock_turn_accum = 0, 0
local lock_light = {}  -- [slot] = {x, y, z, r, g, b}: flat light 0 while the slot is drawn

local function tdiv(a, b)  -- C integer division (toward zero)
    local q = a // b
    if q < 0 and q * b ~= a then q = q + 1 end
    return q
end

local function lock_get(key, i) return sh.session.get("map5_s01.lock_" .. key .. i, 0) end
local function lock_put(key, i, v) sh.session.set("map5_s01.lock_" .. key .. i, v) end

local function lock_code()
    local v = 0
    for i = LOCK.reels - 1, 0, -1 do v = v * 10 + lock_get("reel", i) end
    return v
end

-- 30 fps frames passed this frame (the counters were authored per 30 fps frame).
local function frames30(accum)
    accum = accum + (4096 * sh.delta_time_get()) // 136
    local step = accum // 4096
    return accum - step * 4096, step
end

local function lock_slot_y(i)
    return LOCK.cap_y[i] or (LOCK.reel_y0 - i * LOCK.reel_y_step)
end

local function lock_light_at(i)
    local x, y, z = sh.item_view.get(i, "position")
    local l = lock_light[i] or {0, 0, 0, 0, 0, 0}
    l[1], l[2], l[3] = x, y, z + LOCK.light_z
    lock_light[i] = l
    return l
end

local function lock_load_assets()
    sh.item_view.read("ITEM/FOOK.TIM", "ITEM/FOOK.TMD")
    sh.load.wait()
end

local function lock_init()
    sh.item_view.models_clear()
    lock_state, lock_counter, lock_cursor = 0, 0, 3
    sh.item_view.camera_init()
    -- The set-up's slot loop runs once (its reel loop reuses the counter): only slot 0 gets the
    -- rest pose; every slot gets its yaw from the reels.
    sh.item_view.set(0, "rotation", 0, nil, 0)
    sh.item_view.set(0, "translation", 0, 0, 0)
    sh.item_view.set(0, "rotation", nil, LOCK.cap_yaw, nil)
    sh.item_view.set(5, "rotation", nil, LOCK.cap_yaw, nil)
    for i = 1, LOCK.reels do
        sh.item_view.set(i, "rotation", nil, (lock_get("reel", i - 1) << 9) + LOCK.reel_yaw, nil)
    end
    sh.item_view.models_map()
    for i = 0, LOCK.slots - 1 do
        sh.item_view.model_link(i, i)
        sh.item_view.set(i, "position", 0, lock_slot_y(i), LOCK.z)
    end
    for i = 0, LOCK.slots - 1 do
        local l = lock_light_at(i)
        l[4], l[5], l[6] = 0, 0, 0
    end
    sh.render.ambient_light(256, 256, 256)
    sh.render.light_mode(1)
end

-- The two arrows left and right of the picked reel.
local function lock_arrows()
    local y = -lock_cursor * LOCK.arrow_step
    for i = 0, 1 do
        sh.render.ui_gouraud(9, true,
            -48 + 96 * i, 29 + y, 0xC08000,
            -48 + 96 * i, 45 + y, 0xC08000,
            -64 + 128 * i, 37 + y, 0xFF4000)
    end
end

local function lock_animate(code, cursor)
    local dir = 0
    if lock_state == 0 then
        local c = (lock_counter * 255) // 32
        for i = 0, LOCK.slots - 1 do
            sh.item_view.set(i, "scale", LOCK.scale, LOCK.scale, LOCK.scale)
            local l = lock_light[i]
            l[4], l[5], l[6] = c, c, c
        end
        local step
        lock_fade_accum, step = frames30(lock_fade_accum)
        lock_counter = ((lock_counter + step + 128) & 0xFF) - 128
        if lock_counter >= LOCK.fade_frames then
            lock_state, lock_counter = 1, 0
        end
    elseif lock_state == 1 then
        if lock_get("last", "") ~= code then
            lock_state = 2
        end
        for i = 0, LOCK.slots - 1 do
            sh.item_view.set(i, "position", 0, lock_slot_y(i), LOCK.z)
        end
    elseif lock_state == 2 then
        local step
        lock_turn_accum, step = frames30(lock_turn_accum)
        lock_counter = ((lock_counter + step + 128) & 0xFF) - 128
        local shown, want = lock_get("shown", cursor), lock_get("reel", cursor)
        if (shown < want and (shown ~= 0 or want ~= 7)) or (shown == 7 and want == 0) then
            dir = 1
        else
            dir = -1
        end
        if lock_counter >= LOCK.turn_frames then
            lock_state, lock_counter = 1, 0
            lock_put("last", "", code)
            for i = 0, LOCK.reels - 1 do
                lock_put("shown", i, lock_get("reel", i))
            end
            dir = 0
        end
    end
    local turn = dir * (lock_counter << 9)
    sh.item_view.set(0, "rotation", nil, LOCK.cap_yaw, nil)
    sh.item_view.set(5, "rotation", nil, LOCK.cap_yaw, nil)
    for i = 1, LOCK.reels do
        local shown = lock_get("shown", i - 1)
        local yaw = shown << 9
        if shown ~= lock_get("reel", i - 1) then
            yaw = yaw + tdiv(turn, 16)
        end
        sh.item_view.set(i, "rotation", nil, yaw + LOCK.reel_yaw, nil)
    end
end

-- The round dial face behind the reels: an inner disc and a ring fading out to black.
local function lock_dial_face()
    local inner, outer, n = LOCK.dial_inner, LOCK.dial_outer, LOCK.dial_sectors
    local function at(k, r)
        return (sh.math_sin(k << 8) * r) // 4096, (sh.math_cos(k << 8) * r) // 4096
    end
    local function rgb(r, g, b) return (r << 16) | (g << 8) | b end
    local c = lock_counter
    local centre, edge, ring
    if lock_state == 0 then
        centre = rgb(c * 4, c * 4, (c * 0x70) // 32)
        edge = rgb((c * 0x62) // 32, (c * 0x62) // 32, (c * 0x54) // 32)
        ring = rgb(c * 3, c * 3, (c * 0x54) // 32)
    else
        centre, edge, ring = rgb(0x80, 0x80, 0x70), rgb(0x62, 0x62, 0x54), rgb(0x60, 0x60, 0x54)
    end
    for k = 0, n - 1 do
        local x1, y1 = at(k, inner)
        local x2, y2 = at(k + 1, inner)
        sh.render.ui_gouraud(10, true, 0, 0, centre, x1, y1, edge, x2, y2, edge)
    end
    for k = 0, n - 1 do
        local x0, y0 = at(k, inner)
        local x1, y1 = at(k + 1, inner)
        local x2, y2 = at(k, outer)
        local x3, y3 = at(k + 1, outer)
        sh.render.ui_gouraud(10, true, x0, y0, ring, x1, y1, ring, x2, y2, 0, x3, y3, 0)
    end
    sh.render.ui_draw_mode(10, 1)
end

-- One frame of the lock: input, animation, the models and the dial face. Returns the code
-- while the lock stands still, else 0.
local function lock_update()
    sh.render.ui_frame()
    sh.item_view.precise_depth()
    if lock_state == 1 and sh.event_step_get(0) == 5 then
        if sh.input.pad_direction("down", 3) ~= 0 then
            lock_cursor = lock_cursor + 3
            sh.sound.play("menu_move", 0, 64)
        end
        if sh.input.pad_direction("up", 3) ~= 0 then
            lock_cursor = lock_cursor + 1
            sh.sound.play("menu_move", 0, 64)
        end
        lock_cursor = lock_cursor & 3
        if sh.input.pad_direction("left", 0) ~= 0 then
            sh.sound.play("lock_reel_turn", 0, 64)
            local r = lock_get("reel", lock_cursor) + 1
            lock_put("reel", lock_cursor, r == 8 and 0 or r)
        end
        if sh.input.pad_direction("right", 0) ~= 0 then
            sh.sound.play("lock_reel_turn", 0, 64)
            local r = lock_get("reel", lock_cursor) - 1
            lock_put("reel", lock_cursor, r == -1 and 7 or r)
        end
    end
    local code = lock_code()
    lock_arrows()
    lock_animate(code, lock_cursor)
    sh.item_view.begin()
    for i = 0, LOCK.slots - 1 do
        if sh.item_view.model_linked(i) ~= 0 then
            sh.item_view.set(i, "rotation", LOCK.tilt, nil, 0)
            sh.item_view.model_rotate(i)
            local l = lock_light[i]
            if lock_state ~= 0 then
                l = lock_light_at(i)
                l[4], l[5], l[6] = 255, 255, 255
            end
            sh.render.flat_light(0, l[1], l[2], l[3], l[4], l[5], l[6])
            sh.item_view.model_draw(i, 0)
        end
    end
    sh.item_view.finish()
    lock_dial_face()
    return lock_state == 1 and code or 0
end

local dialCodeSteps = {}

dialCodeSteps[0] = function()
    sh.player_freeze(true)
    lock_load_assets()
    sh.cutscene.border_show()
    sh.event_fade_cmd(0, true, 2, 0, false)
    sh.npc.set_pos_x(0, 212992) -- 52.0 m
    sh.npc.set_pos_z(0, -236748) -- -57.8 m
    sh.npc.set_rot_y(0, 1224) -- 107.6 deg
    sh.vec.set(0, 203980, -6144, -245760) -- camera position 49.8, -1.5, -60.0
    sh.vec.set(1, 215040, -5324, -237158) -- camera look-at 52.5, -1.3, -57.9
    sh.camera.position("vec:0", 0, 0, 0, 0, 0, 0, 0, true)
    sh.camera.look_at("vec:1", 0, 0, 0, 0, 0, 0, 0, true)
    sh.event_step_increment(0)
    return dialCodeSteps[1]()
end

dialCodeSteps[1] = function()
    sh.event_wait_timer(4096, false) -- 1.0 s
end

dialCodeSteps[2] = function()
    sh.event_fade_cmd(2, true, 0, 0, false)
end

dialCodeSteps[3] = function()
    if sh.load.chunks() ~= 0 then
        sh.sound.set_bgm_flags(sh.sound.bgm_flags() | 1) -- pause the music
        lock_init()
        sh.event_step_increment(0)
    end
    return dialCodeSteps[4]()
end

dialCodeSteps[4] = function()
    sh.sound.set_bgm_flags(sh.sound.bgm_flags() | 1)
    sh.event_fade_cmd(2, false, 0, 0, false)
end

dialCodeSteps[5] = function(temp)
    sh.game.timer_update()
    sh.sound.set_bgm_flags(sh.sound.bgm_flags() | 1)
    local r = lock_update() -- signed 16-bit result
    r = ((r + 0x8000) & 0xFFFF) - 0x8000
    if not sh.input_action("cancel", 1) then
        if temp ~= r then
            return
        end
        sh.flag_set("M5S01_DialLockOpened", true)
        sh.sound.play("dial_click", 0, 128) -- sound dial_click, 0.5
    end
    sh.event_step_increment(0)
end

dialCodeSteps[6] = function()
    sh.sound.set_bgm_flags(sh.sound.bgm_flags() | 1)
    lock_update()
    sh.event_fade_cmd(2, true, 0, 0, false)
end

dialCodeSteps[7] = function()
    sh.event_fade_cmd(2, false, 0, 0, false)
    return dialCodeSteps.default()
end

dialCodeSteps.default = function()
    sh.event_fade_cmd(0, false, 2, 0, false)
    sh.camera.auto_return(true)
    sh.player_freeze(false, false)
    sh.state_set(0)
    sh.sound.radio_npc_clear()
end

-- index 5: dial code puzzle (lock_update, code 0x1D9).
function Puzzle_DialCode()
    local temp = 0x1D9

    if sh.event_step_get(0) > 2 then
        sh.sound.radio_stop()
    end

    local s = sh.event_step_get(0)
    runStep(dialCodeSteps, s, temp)
end

-- index 6: Kaufmann key used: the key is removed, a message with the key sound at the
-- event point (1.2 m up), flag M5S01_KaufmannKeyUsed.
function MapEvent_KaufmannKeyUse()
    sh.player.item_remove("KaufmannKey", 1)
    sh.event_msg_with_sfx("map5_s01.used_the_kaufmann_key", "use_key")     -- "Used the Kaufmann key.", use key sound
    sh.flag_set("M5S01_KaufmannKeyUsed", true)             -- flag: Kaufmann key used
end

-- index 7: UFO over the lighthouse, Channeling Stone (UFO3.DMS).
-- The voice line index, the beam stage, the hum volume and the hum clock are used only by
-- this handler, so they are kept local. ufo_glow_tex, ufo_pos and ufo_msg_voice_cmds
-- are map state.
local s_ufoMsgAudioIdx = 0
local s_ufoBeamStage = 0
local s_ufoHumVol = 0
local s_ufoHumClock = 0

local Ufo3Buf = 13 -- file buffer 13

-- axis: "ufo_pos.y" / "ufo_pos.z"
local function ufoBeamAxisAdd(axis, delta)
    sh.map.set(axis, sh.map.get(axis) + delta)
end

local ufoChannelingStoneSteps = {}

ufoChannelingStoneSteps[0] = function()
    sh.player_freeze(true)
    sh.load.tim("TIM/ENBAN.TIM", 1, "ufo_glow_tex") -- ENBAN.TIM
    sh.load.file("ANIM/UFO3.DMS", 13) -- UFO3.DMS
    sh.map.set("cutscene_timer", -1) -- none
    sh.screen_fade_reset_timestep()
    sh.sd_call("ufo_hum")
    sh.sound.attributes("ufo_hum", 0, 255, 0) -- 1.0f
    s_ufoHumVol = 0
    s_ufoHumClock = 0
    s_ufoBeamStage = 0
    sh.event_step_increment(0)
    return ufoChannelingStoneSteps[1]()
end

ufoChannelingStoneSteps[1] = function()
    sh.event.wait_player_stop()
end

ufoChannelingStoneSteps[2] = function()
    if sh.load.chunks() ~= 0 then
        sh.event_step_increment(0)
    end
end

ufoChannelingStoneSteps[3] = function()
    sh.map.set("cutscene_timer", 0)
    sh.event_fade_cmd(0, true, 3, 0, false)
    sh.cutscene.prepare(13)
    sh.held_item_hide()
    sh.player.weapon_stash()
    sh.light.flashlight_on()
    s_ufoMsgAudioIdx = 0
    sh.event_step_increment(0)
    return ufoChannelingStoneSteps[4]()
end

ufoChannelingStoneSteps[4] = function()
    sh.event_fade_cmd(2, false, 0, 0, false)
end

ufoChannelingStoneSteps[5] = function()
    -- UFO message with voice; the line index is kept here and the table entry is read only
    -- when a page starts.
    s_ufoMsgAudioIdx = sh.event_msg_audio("map5_s01.what", s_ufoMsgAudioIdx, "ufo_msg_voice_cmds")
end

ufoChannelingStoneSteps[6] = function()
    -- 40.0, 0.0, 30.0
    sh.cutscene.timer_advance("cutscene_timer", 163840, 0, 122880, true, true)
    sh.map.ufo_glow_spawn("ufo_pos", 2048, 204, 0) -- 0.5, 0.05 m
end

ufoChannelingStoneSteps[7] = function()
    sh.msg.show(false, "map5_s01.that_light", 0, 0, 0, false)
    sh.map.ufo_glow_spawn("ufo_pos", 2048, 204, 0)
end

ufoChannelingStoneSteps[8] = function()
    sh.vec.short_set(0, 0, 0, 0) -- no rotation
    sh.vec.set(0, -804986, -58368, -39854) -- (-196.53, -14.25, -9.73) m
    sh.camera.look_at_bone("player_bone:2", "svec:0", "vec:0") -- Harry's head
    sh.map.ufo_glow_spawn("ufo_pos", 4096, 409, 1) -- 1.0, 0.1 m
    sh.event_wait_timer(10240, false) -- 2.5 s
end

ufoChannelingStoneSteps[9] = function()
    sh.event.set_state_timer(sh.event.state_timer() + sh.delta_time_get())
    local phase = sh.event.state_timer() // 409 -- the phase changes every 0.1 s (the timer is never negative)
    local mulX, mulZ
    if phase >= 0 and phase <= 3 then
        mulZ = 0; mulX = 0
    elseif phase == 4 or phase == 5 or phase == 6 or phase == 11 or phase == 12 or phase == 13
           or phase == 18 or phase == 19 or phase == 20 then
        mulX = -3686; mulZ = 4096 -- -0.9, 1.0
    elseif (phase >= 7 and phase <= 10) or (phase >= 14 and phase <= 17) or (phase >= 21 and phase <= 24) then
        mulX = 4915; mulZ = 4096 -- 1.2, 1.0
    elseif phase == 25 or phase == 26 or phase == 27 or phase == 31 or phase == 32 or phase == 33
           or phase == 37 or phase == 38 or phase == 39 or phase == 43 or phase == 44 or phase == 45 then
        if phase == 25 and s_ufoBeamStage == 0 then -- the first time phase 25 is reached: move the UFO
            sh.map.set("ufo_pos.y", -53248) -- height -13.0
            sh.map.set("ufo_pos.z", 10240)  -- depth 2.5
            s_ufoBeamStage = s_ufoBeamStage + 1
        end
        mulX = -4915; mulZ = -4915 -- -1.2
    elseif (phase >= 28 and phase <= 30) or (phase >= 34 and phase <= 36) or (phase >= 40 and phase <= 42) then
        mulX = 4915; mulZ = -4915
    elseif phase == 46 or phase == 59 then
        if phase == 46 and s_ufoBeamStage == 1 then -- the first time phase 46 is reached: move the UFO
            sh.map.set("ufo_pos.y", -64512) -- height -15.75
            sh.map.set("ufo_pos.z", -50176) -- depth -12.25
            s_ufoBeamStage = s_ufoBeamStage + 1
        end
        mulX = 0; mulZ = -6553 -- -1.6
    elseif (phase >= 47 and phase <= 49) or (phase >= 56 and phase <= 58) then
        mulX = 2457; mulZ = -6553 -- 0.6
    elseif phase >= 50 and phase <= 55 then
        mulX = -2457; mulZ = -6553 -- -0.6
    else
        mulZ = 0; mulX = 0
        sh.event_step_increment(0)
    end
    local dt = sh.delta_time_get()
    ufoBeamAxisAdd("ufo_pos.y", (dt * (mulX * 4)) // 4096) -- the UFO height moves at mulX * 4 per second
    ufoBeamAxisAdd("ufo_pos.z", (dt * (mulZ * 4)) // 4096) -- .vz
    sh.map.ufo_glow_spawn("ufo_pos", 2048, 204, 0)
end

ufoChannelingStoneSteps[10] = function()
    sh.event_wait_timer(10240, false)
end

ufoChannelingStoneSteps.default = function()
    sh.player_freeze(false, false)
    sh.state_set(0)
    sh.event_fade_cmd(0, false, 2, 0, false)
    sh.camera.auto_return(true)
    sh.flag_set("M5S01_ChannelingStoneUsed", true)
    sh.event.inv_item(3, "ChannelingStone", 1, false) -- AddItem, channeling stone
    sh.held_item_show()
    sh.player.weapon_restore()
    sh.map.set("cutscene_timer", -1)
    sh.sfx_stop("ufo_hum")
end

function Cutscene_UfoChannelingStone()
    local clock = s_ufoHumClock
    local sin0 = sh.math_sin(clock // 4)
    local sin1 = sh.math_sin(clock // 8)
    local wave = ((sin0 * 2) + sin1) + sh.math_sin(sh.math_cos(clock // 16))
    local pitch = wave // 256
    s_ufoHumClock = s_ufoHumClock + sh.delta_time_get()

    if sh.event_step_get(0) >= 10 then
        s_ufoHumVol = s_ufoHumVol - (sh.delta_time_get() * 2048) // 4096 -- dt * 0.5
        if s_ufoHumVol < 0 then
            s_ufoHumVol = 0
        end
        sh.sound.attributes("ufo_hum", 0, ~(s_ufoHumVol // 16), pitch)
    elseif sh.event_step_get(0) > 0 then
        s_ufoHumVol = s_ufoHumVol + (sh.delta_time_get() * 1024) // 4096 -- 0.25 s
        if s_ufoHumVol > 4080 then -- 0.9961
            s_ufoHumVol = 4080
        end
        sh.sound.attributes("ufo_hum", 0, ~(s_ufoHumVol // 16), pitch)
    end

    local s = sh.event_step_get(0)
    runStep(ufoChannelingStoneSteps, s)

    local t = sh.map.get("cutscene_timer")
    if t >= 0 then
        sh.cutscene.chara(0, "HERO", t, Ufo3Buf)
        sh.npc.set_pos_x(0, sh.npc.pos_x(0) - 81920) -- 20.0 m
        sh.camera.projection(sh.cutscene.camera_target("camera_target_pos", "camera_target_look", nil, t, 13))
        sh.map.set("camera_target_pos.x", sh.map.get("camera_target_pos.x") - 81920)
        sh.map.set("camera_target_look.x", sh.map.get("camera_target_look.x") - 81920)
        sh.camera.user_target("camera_target_pos", nil, true)
        sh.camera.user_watch("camera_target_look", nil, true)
    end
end
