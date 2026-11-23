-- Set-up and per-tick rules of map6_s00: the ground texture of the
-- loading screen, the light and the flashlight, the npc flags id, the flag latch and the collision trigger bits.

-- Before the objects (the start of Map_WorldObjectsInit): until M6S00_OtherworldSpreadsCutsceneDone the next loading screen shows the
-- ground.
if not sh.flag_get("M6S00_OtherworldSpreadsCutsceneDone") then
    sh.load.loading_screen_image("TIM/GROUND.TIM")
end

-- After the objects (the rest of Map_WorldObjectsInit): no water, the flashlight's attributes, full light,
-- the npc flags id by difficulty, and the flashlight back on when M6S02_FlashlightRestorePending asks for it (once).
function post_init()
    sh.world.water_disable()
    sh.light.flashlight_fix()
    sh.light_intensity_set(4096)
    local difficulty = sh.save.game_difficulty()
    sh.game.set_npc_flags_id(difficulty == 0 and 4 or (difficulty == -1 and 3 or 5)) -- Normal 4, Easy 3, else 5
    if sh.flag_get("M6S02_FlashlightRestorePending") then
        sh.flag_set("M6S02_FlashlightRestorePending", false)
        sh.light.flashlight_on()
    end
end

-- The map's chunk test (PLAYER_IN_MAP_CHUNK): the index truncates toward zero; the offsets differ by sign.
local CELL = 163840 -- 40 m
local function in_chunk(p, off_pos, want_pos, off_neg, want_neg)
    local idx = p // CELL
    if p < 0 and idx * CELL ~= p then
        idx = idx + 1
    end
    if p > 0 then
        return idx + off_pos == want_pos
    end
    return idx + off_neg == want_neg
end

function update(dt)
    local flags = 0
    if not sh.flag_get("M6S00_AfterOtherworldAutosaveTaken") and sh.flag_get("M6S00_AfterOtherworldWarpDone") then
        sh.flag_set("M6S00_AfterOtherworldAutosaveTaken", true)
        sh.game.state_update()
    end
    local x, _, z = sh.player_pos()
    if in_chunk(x, 1, -1, 0, 0) and in_chunk(z, 1, 7, -1, 7) then
        if not sh.flag_get("M6S00_OtherworldSpreadsCutsceneDone") then
            flags = flags | 2 -- CollisionTriggerFlag_1
        end
    end
    if in_chunk(x, 1, -5, -1, -5) then
        if in_chunk(z, 1, 4, -1, 4) or in_chunk(z, 1, 3, -1, 3) then
            if sh.flag_get("M6S01_CybilDahliaCutsceneDone") then
                flags = flags | 2 -- CollisionTriggerFlag_1
            end
        end
    end
    sh.collision.flag_bits_clear(0xFFFF)
    sh.collision.flag_bits_set(flags)
end
