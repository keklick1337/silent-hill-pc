-- World objects of map6_s03: the hanging body (Sitai) sways while the
-- player is in its cell (x 1, z 0). Each frame a random walk turns it about y around 180 degrees and pushes
-- it along z, never in front of its rest spot. The random numbers are the game's (sh.rng_int(0, 0x7FFF) is
-- one Rng_Rand16), drawn in the original's order; nothing else in the map's world update draws any before
-- them, so running this at the start of the update keeps the game's random sequence.

local CELL = 163840 -- 40 m
local REST_Z = 80691 -- Q12(19.7)

-- The original's state, reset when the map loads: turn (s16), its step direction and size (s8), push
-- (s16), its step (s8).
local turn, turn_step, push, push_step = 0, 0, 0, 0

local function rand16()
    return sh.rng_int(0, 0x7FFF)
end

local function s16(v)
    v = v & 0xFFFF
    return v >= 0x8000 and v - 0x10000 or v
end

function update(dt)
    local x, _, z = sh.player_pos()
    -- Positive positions only: truncation = floor there.
    if not (x > 0 and x // CELL == 1 and z > 0 and z // CELL == 0) then
        return
    end

    local r = (rand16() & 0x1F) + 0x20
    if turn_step > 0 and turn > 0 and r < turn then
        turn_step = -(rand16() & 1) - 1
    elseif turn_step < 0 and turn < 0 and r < -turn then
        turn_step = (rand16() & 1) + 1
    elseif turn_step == 0 or (rand16() & 0x1F) == 0 then
        turn_step = (rand16() % 5) - 2
    end
    if (rand16() & 0xFF) < (math.abs(turn_step) * 64 + 0x40) then
        turn = s16(turn_step <= 0 and turn - 1 or turn + 1)
    end

    -- The arithmetic shift right of the original is a floor division.
    r = (rand16() & 0x1F) + 0x10
    if push_step > 0 and r < push + math.abs(turn // 2) then
        push_step = -(rand16() & 1) - 1
    elseif push_step == 0 or (rand16() & 0x3F) == 0 then
        push_step = (rand16() % 5) - 2
    end
    if (rand16() & 0xFF) < (math.abs(push_step) * 64 + 0x60) then
        push = s16(push_step <= 0 and push - 1 or push + 1)
    end
    push = math.max(0, push)

    local rx, _, rz = sh.world_object.rotation_get("Sitai")
    sh.world_object.rotation_set("Sitai", rx, turn + 0x800, rz)
    local px, py = sh.world_object.position_get("Sitai")
    sh.world_object.position_set("Sitai", px, py, push + REST_Z)
end

-- Per-tick rules; they run after the sway above.
local NPC_COUNT = 0
local SFX_NO_DIST_ATTEN = 2
local SPAWN_FLAGS = 6 -- SpawnFlag_1 | SpawnFlag_2

local function rules()
    if NPC_COUNT == 0 then NPC_COUNT = sh.npc.slot_count() end
    if not sh.flag_get("M6S03_ArrivalAutosaveTaken") and sh.flag_get("M6S03_AfterHatchDescendWarpDone") then
        sh.flag_set("M6S03_ArrivalAutosaveTaken", true)
        sh.game.state_update()
    end

    sh.collision.flag_bits_clear(0xFFFF)

    local x = sh.player_pos()
    -- The original's chunk test is "x <= 0" in effect (any positive x fails it, any other passes).
    if x <= 0 then
        if not sh.flag_get("M6S03_FloorChangeSoundPlayed") and sh.flag_get("M6S03_SewerFloorChanged") then
            sh.sfx_flags_play("sewer_floor_change_noise", -81920, 0, 139264, 128, 0)
            sh.flag_set("M6S03_FloorChangeSoundPlayed", true)
            for slot = 1, NPC_COUNT do
                if sh.npc.chara_id(slot) ~= 0 and sh.npc.pos_z(slot) < 149504 then -- 36.5 m
                    sh.npc.remove_saved(slot)
                end
            end
        end
        if sh.flag_get("M6S03_SewerFloorChanged") then
            sh.collision.flag_bits_set(12) -- CollisionTriggerFlag_2 | CollisionTriggerFlag_3
        else
            sh.collision.flag_bits_set(2) -- CollisionTriggerFlag_1
        end
    end

    if not sh.flag_get("M6S03_RearEnemiesCleared") and x > 196608 then -- 48 m
        if sh.view.aabb_visible(40, 44, -2, 0, 18, 22) == 0 then
            sh.flag_set("M6S03_RearEnemiesCleared", true)
            for slot = 1, NPC_COUNT do
                if sh.npc.pos_x(slot) < 131072 and sh.npc.chara_id(slot) ~= 0 then -- 32 m
                    sh.npc.clear(slot, 0, 0)
                end
            end
            sh.npc.spawn("HangedScratcher", 9, 172851, 85606, 1024, 6)
            sh.chara.spawn_flags_set("HangedScratcher", 9, SPAWN_FLAGS)
            sh.npc.spawn("HangedScratcher", 10, 168345, 77824, 1024, 6)
            sh.chara.spawn_flags_set("HangedScratcher", 10, SPAWN_FLAGS)
            sh.sfx_flags_play("sewer_ambush_noise", -170393, 0, 81920, 255, SFX_NO_DIST_ATTEN)
        end
    end
end

local update_sway = update
function update(dt)
    update_sway(dt)
    rules()
end

-- Set-up when the map loads (the end of Map_WorldObjectsInit): Easy uses the npc flags id 3, the other
-- difficulties keep the one the map load set.
if sh.save.game_difficulty() == -1 then
    sh.game.set_npc_flags_id(3)
end
