-- map1_s04: unused area
-- Event scripts of this area: the engine runs a handler every tick while its event is active.

function on_boot()
    -- per-map script entry point
end

-- index 1: the debug sound test. Page 1: the left stick picks a row (0..22) and a column of
-- the voice table, Circle plays it, R3 goes back to 0/0, Cross stops the play clock; Start
-- (the skip key) goes to page 2, where the stick picks one of four sound effects that Circle
-- plays and Cross stops. Only the text position of the PS1 proto's readout is left in retail.
local SoundTestRows = 23
local SoundTestColumns = 40

-- The PC copy of the voice table holds its first 128 ids; the rest read 0.
local function SoundTestId(row, column)
    local i = row * SoundTestColumns + column
    if i < 0 or i >= 128 then
        return 0
    end
    return sh.map.get("sound_test_ids", i)
end

local function Clicked(button)
    return sh.pad.button(button, 1) ~= 0
end

local function Pulsed(button)
    return sh.pad.button(button, 3) ~= 0
end

function MapEvent_DebugSoundTest()
    local step = sh.event_step_get(0)
    if step == 0 then
        sh.player_freeze(true)
        sh.cutscene.border_show()
        sh.event_step_increment(0)
        step = 1 -- continues into step 1 in the same tick
    end

    if step == 1 then
        if Clicked("R3") then
            sh.map.set("sound_test_row", 0)
            sh.map.set("sound_test_column", 0)
        end

        if Pulsed("LStickDown") then
            if sh.map.get("sound_test_row") == 0 then
                sh.map.set("sound_test_row", SoundTestRows)
            end
            sh.map.set("sound_test_row", sh.map.get("sound_test_row") - 1)
        end

        if Pulsed("LStickUp") then
            sh.map.set("sound_test_row", sh.map.get("sound_test_row") + 1)
            if sh.map.get("sound_test_row") == SoundTestRows then
                sh.map.set("sound_test_row", 0)
            end
        end

        if Pulsed("LStickLeft") then
            if sh.map.get("sound_test_column") ~= 0 then
                sh.map.set("sound_test_column", sh.map.get("sound_test_column") - 1)
            end
        end

        if Pulsed("LStickRight") then
            local column = sh.map.get("sound_test_column")
            if column ~= SoundTestColumns - 1 and SoundTestId(sh.map.get("sound_test_row"), column + 1) ~= 0 then
                sh.map.set("sound_test_column", column + 1)
            end
        end

        if Clicked("Circle") then
            sh.sound.sd_call(SoundTestId(sh.map.get("sound_test_row"), sh.map.get("sound_test_column")))
            sh.map.set("sound_test_time", 0)
            sh.map.set("sound_test_state", 1)
        end

        -- 1: the voice was requested; 2: it streams, the clock runs until Cross.
        local state = sh.map.get("sound_test_state")
        if state == 1 and sh.sound.streaming() ~= state then
            sh.map.set("sound_test_state", 2)
        end

        if sh.map.get("sound_test_state") == 2 and sh.sound.streaming() == 1 then
            sh.map.set("sound_test_time", sh.map.get("sound_test_time") + sh.game.delta_time())
            if Clicked("Cross") then
                sh.map.set("sound_test_state", 0)
            end
        end

        sh.screen.debug_text_position(50, 50)

        if sh.input_action("skip", 1) then
            sh.event_step_increment(0)
        end
    elseif step == 2 then
        if Clicked("Circle") then
            sh.sound.sd_call(sh.map.get("sound_test_sfx_ids", sh.map.get("sound_test_row")))
        end

        if Clicked("Cross") then
            sh.sound.stop(sh.map.get("sound_test_sfx_ids", sh.map.get("sound_test_row")))
        end

        if Pulsed("LStickDown") then
            if sh.map.get("sound_test_row") == 0 then
                sh.map.set("sound_test_row", 4)
            end
            sh.map.set("sound_test_row", sh.map.get("sound_test_row") - 1)
        end

        if Pulsed("LStickUp") then
            sh.map.set("sound_test_row", sh.map.get("sound_test_row") + 1)
            if sh.map.get("sound_test_row") == 4 then
                sh.map.set("sound_test_row", 0)
            end
        end
    end
end
