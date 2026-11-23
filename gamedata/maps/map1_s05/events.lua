-- map1_s05: School - boss fight (Split Head)
-- Event scripts of this area: the engine runs a handler every tick while its event is active.
-- Fixed-point numbers: 4096 = 1.0 (metres, seconds); angles: 4096 = 360 degrees.

function on_boot()
    -- per-map script entry point
end

-- Fixed-point world units -> metres for camera_pos/look.
local function M(q) return q / 4096 end

-- index 2: corridor light flash (lightIntensity 0 -> 2.25 -> 1.3843), flags 129/130.
-- Map state (sh.map.get/set): particle_emitter.state/.intensity and effect_slot_type[500]
-- (the map effect slot type).
local EFFECT_SLOT_COUNT = 500

function Cutscene_CorridorLightFlash()
    if sh.input_action("skip", 1) and sh.event_step_get(0) >= 3 then
        sh.map.set("particle_emitter.state", 0)
        sh.map.set("particle_emitter.intensity", 0)
        for i = 0, EFFECT_SLOT_COUNT - 1 do
            local slotType = sh.map.get("effect_slot_type", i)
            if slotType < 12 and slotType >= 8 then
                sh.map.set("effect_slot_type", 12, i)
            end
        end
        sh.camera_auto_return(true)
        sh.event_step_reset()
    end

    local step = sh.event_step_get(0)
    if step == 0 or step == 1 then
        if step == 0 then
            sh.player_freeze(true)
            sh.event_fade_cmd(0, 0, 0, 0, 0)               -- Start
            sh.cutscene_border(20)                         -- force the cutscene borders on
            sh.light_intensity_set(0)
            sh.camera_pos(M(80773), M(-21299), M(-113049), 0, 0, 0, 0, 0, 0, 0, true) -- 19.72, -5.2, -27.6 m
            sh.camera_look(M(80773), M(-13516), M(-98304), 0, 0, 0, 0, 0, 0, 0, true) -- 19.72, -3.3, -24.0 m
            sh.event_step_increment(0)
        end
        sh.event_wait_timer(6144, 0)                       -- 1.5 s
    elseif step == 2 or step == 3 then
        if step == 2 then
            sh.map.set("particle_emitter.state", 3)
            sh.event_step_increment(0)
        end
        sh.event_wait_timer(819, 0)                        -- 0.2 s
    elseif step == 4 then
        local v = sh.light_intensity_get() + (sh.delta_time_get() * 921) // 4096 -- dt * 0.225
        sh.light_intensity_set(v)
        if ((v + 0x8000) % 0x10000) - 0x8000 > 9216 then   -- signed 16-bit value > 2.25
            sh.light_intensity_set(9216)
            sh.event_step_increment(0)
        end
    elseif step == 5 then
        local v = sh.light_intensity_get() - (sh.delta_time_get() * 1843) // 4096 -- dt * 0.45
        sh.light_intensity_set(v)
        if ((v + 0x8000) % 0x10000) - 0x8000 < 5670 then   -- < 1.3843
            sh.flag_set("M1S05_BossLampFlickering", true)
            sh.event_step_reset()
        end
    else
        sh.player_freeze(false)
        sh.state_set(0)
        sh.light_intensity_set(5670)                       -- 1.3843
        sh.event_fade_cmd(0, 0, 2, 0, 0)
        sh.camera_auto_return(false)
        sh.flag_set("M1S05_SplitHeadIntroDone", true)
        sh.game_state_update()
    end
end

-- index 3: Split Head defeated (flag M1S05_SplitHeadKilled). The room light swells to 3.0 for 1.5 s, the hum
-- (darkness_hum) restarts in pulses while the light sinks to 0, and every frame is drawn
-- over the next at half blend (the ghosting); then a 2.5 s fade out, flag M1S05_SplitHeadDefeatSceneDone (the event
-- table loads map1_s06 on it).
local LIGHT_MAX = 12288                                   -- 3.0
local defeatPulse = 0                                     -- an overlay u8 in the original

local function s16(v) return ((v + 0x8000) % 0x10000) - 0x8000 end -- lightIntensity is q3_12

function Cutscene_SplitHeadDefeated()
    local step = sh.event_step_get(0)
    if step > 0 and step < 5 then
        sh.render.frame_hold{source = "previous", tint = 128, blend = "half", layer = "overlay"}
    end

    if step == 0 or step == 1 then
        if step == 0 then
            sh.flag_set("M1S05_BossLampFlickering", false)
            defeatPulse = 1
            sh.event_step_increment(0)
        end
        sh.event_wait_timer(6144, 0)                       -- 1.5 s
        local v = s16(sh.light_intensity_get() + (sh.delta_time_get() * 819) // 4096) -- dt * 0.2
        sh.light_intensity_set(v)
        if v > LIGHT_MAX then
            sh.light_intensity_set(LIGHT_MAX)
        end
    elseif step == 2 or step == 3 then
        if step == 2 then
            sh.map.set("particle_emitter.state", 1)
            sh.map.set("particle_emitter.unread_a", 3)
            sh.event_step_increment(0)
        end
        if (defeatPulse & 1) ~= 0 then
            sh.sd_call("darkness_hum")
            defeatPulse = (defeatPulse + 1) & 0xFF
        end
        local volume = ((sh.light_intensity_get() & 0xFFF) >> 4) - defeatPulse * 8
        sh.sfx_attributes_update("darkness_hum", 0, math.max(0, volume), 0)

        local v = s16(sh.light_intensity_get() - (sh.delta_time_get() * 614) // 4096) -- dt * 0.15
        sh.light_intensity_set(v)
        if v < (6 - defeatPulse) * 2048 then               -- each 0.5 below 3.0 is one pulse
            defeatPulse = (defeatPulse + 1) & 0xFF
        end
        if v < 0 then
            sh.light_intensity_set(0)
            sh.event_step_increment(0)
        end
    elseif step == 4 then
        sh.event_fade_cmd(2, 1, 0, 10240, 0)               -- Auto, out, 2.5 s
    else
        sh.player_freeze(false, false)
        sh.state_set(0)                                    -- gameplay
        sh.flag_set("M1S05_SplitHeadDefeatSceneDone", true)
    end
end
