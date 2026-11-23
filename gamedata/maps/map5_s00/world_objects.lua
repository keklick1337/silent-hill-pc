-- Per-tick sound rule of map5_s00.
local CELL = 163840   -- 40 m
local SFX_NO_DIST_ATTEN = 2

-- Chunk index as the C code computes it: integer division truncates toward zero.
local function cell(p)
    local c = p // CELL
    if p < 0 and c * CELL ~= p then
        c = c + 1
    end
    return c
end

-- Only the non-positive branch of the original chunk test can match here (the positive offsets never hit).
local function in_chunk_neg(p, off, want)
    return p <= 0 and cell(p) + off == want
end

function update(dt)
    local x, _, z = sh.player_pos()
    if in_chunk_neg(x, -1, -3) and in_chunk_neg(z, -1, -2) and not sh.flag_get("M5S00_TunnelSoundPlayed") then
        if sh.flag_get("M5S00_DrainKeyAmbushCutsceneDone") and z < -270336 then -- -66 m
            sh.sfx_flags_play("sewer_ambush_noise", -376627, 0, -336691, 255, SFX_NO_DIST_ATTEN)
            sh.flag_set("M5S00_TunnelSoundPlayed", true)
        end
    end
end

-- Set-up when the map loads (the end of Map_WorldObjectsInit): the npc flags id by difficulty.
do
    local difficulty = sh.save.game_difficulty()
    sh.game.set_npc_flags_id(difficulty == -1 and 3 or 4) -- Easy 3, Normal and Hard 4
end

-- The map's npcSpawnEvent, before the room's spawn pass: after the sewer scene (M5S00_DrainKeyAmbushCutsceneDone) the hanged
-- Scratchers wait in the tunnels (SpawnFlag_1 | SpawnFlag_2), one more beyond Easy, and the one of row
-- 12 hangs elsewhere once map mark MapMark_FogSewer2F_OuterRightArrows is drawn; after the sewer key's pickup row 6 spawns too
-- (SpawnFlag_0 | SpawnFlag_3).
local HANGED = "HangedScratcher"
local HANGING = 6
local AFTER_KEY = 9

function npc_spawn()
    if sh.flag_get("M5S00_DrainKeyAmbushCutsceneDone") then
        sh.chara.spawn_flags_set(HANGED, 0, HANGING)
        sh.chara.spawn_flags_set(HANGED, 1, HANGING)
        sh.chara.spawn_flags_set(HANGED, 2, HANGING)
        if sh.save.game_difficulty() ~= -1 then
            sh.chara.spawn_flags_set(HANGED, 11, HANGING)
        end
        if sh.flag_get("MapMark_FogSewer2F_OuterRightArrows") then
            sh.npc.spawn_position_set(sh.chara_id(HANGED), 12, -369868, -380518) -- (-90.3, -92.9) m
        end
        sh.chara.spawn_flags_set(HANGED, 12, HANGING)
        sh.chara.spawn_flags_set(HANGED, 13, HANGING)
    end
    if sh.flag_get("M5S00_PickupSewerKey") then
        sh.chara.spawn_flags_set(HANGED, 6, AFTER_KEY)
    end
end

-- Drain sparkles (the light on the water at the drain, about (-92, 0.5, 8) m): one grid slot
-- (variant 0, a 4x4 grid of 8x8-texel glints) and 12 sparkle slots (variant 1) of effect slot
-- type 32, taken when the map loads (post_init, after the objects); the 2D effects pass calls
-- effect_slot_draw for each.
local SPARKLE_SLOT = 32
local SPARKLE_Y = 2048     -- 0.5 m
local GRID_X0 = -382976    -- -93.5 m, west edge of the grid
local GRID_Z0 = 39321      -- 9.6 m, north edge of the grid
local GRID_STEP = 3072     -- 0.75 m
local GRID_TEXTURE = {page = {704, 0}, palette = {304, 10}, colours = 16}
local SPARKLE_TEXTURE = {page = {704, 0}, palette = {304, 0}, colours = 16}
local CLIP = {200, 160}

local function slot_get(slot, field) return sh.world.effect_slot_get(slot, field) end
local function slot_set(slot, field, v) sh.world.effect_slot_set(slot, field, v) end

function post_init()
    local slot = sh.world.effect_slot_alloc(SPARKLE_SLOT)
    if slot < 0 then
        return
    end
    slot_set(slot, "variant", 0)
    slot_set(slot, "u8_2", sh.rng.int(0, 2))             -- texture row
    for _ = 1, 12 do
        slot = sh.world.effect_slot_alloc(SPARKLE_SLOT)
        if slot < 0 then
            break
        end
        slot_set(slot, "variant", 1)
        slot_set(slot, "x", sh.rng.int(-379904, -373761))   -- -92.75 .. -91.25 m
        slot_set(slot, "y", SPARKLE_Y)
        slot_set(slot, "z", sh.rng.int(30105, 36248))       -- 7.35 .. 8.85 m
        slot_set(slot, "u16_0", sh.rng.uint(0, 4095))       -- brightness
        slot_set(slot, "u8_2", sh.rng.int(0, 2))
    end
end

-- 5x5 grid points, row by row from the north-west corner, then the two points past its end:
-- the original projected its grid in threes, and the last three reached two points beyond the
-- grid, whose depths stay in the projection unit's depth FIFO; every glint is depth-sorted by
-- that FIFO (points 25, 26, 27, 25).
local function grid_points()
    local p = {}
    for k = 0, 26 do
        p[#p + 1] = GRID_X0 + (k % 5) * GRID_STEP
        p[#p + 1] = SPARKLE_Y
        p[#p + 1] = GRID_Z0 - (k // 5) * GRID_STEP
    end
    return p
end
local GRID_POINTS = grid_points()
local GRID_SORT = {25, 26, 27, 25}

local function draw_grid(row)
    -- A faint red glint once the light leaves the drain darker than 128, none otherwise.
    local tint = {sh.world.darkness_at(-376832, 2048, 33177) < 129 and 0 or 8, 0, 0}   -- (-92, 0.5, 8.1) m
    local quads = {}
    for i = 0, 3 do
        for j = 0, 3 do
            local p = i * 5 + j + 1
            local u, v = 224 + j * 8, row + i * 8
            quads[#quads + 1] = {
                corners = {p, p + 1, p + 5, p + 6},
                uv = {u, v, u + 7, v, u, v + 7, u + 7, v + 7},
                tint = tint, depth_test = 1, screen_test = 1, precise = false, sort_points = GRID_SORT,
            }
        end
    end
    sh.render.world_quads{points = GRID_POINTS, quads = quads, texture = GRID_TEXTURE, blend = "add",
                          depth_bias = 128, clip = CLIP}
end

local function draw_sparkle(slot, row)
    -- The brightness is unsigned: a step below 0 wraps round and clamps to full.
    local b = math.min((slot_get(slot, "u16_0") + sh.rng.uint(-32, 31)) & 0xFFFF, 4095)
    slot_set(slot, "u16_0", b)
    local half = (b * 6144) // 4096                       -- half the size: brightness * 1.5
    local x, y, z = slot_get(slot, "x"), slot_get(slot, "y"), slot_get(slot, "z")
    local level = math.min(math.max(((4096 - b) * 32) // 4096, 0), 16)
    -- The original copies the fourth corner in from a scratch store, so it has no sub-pixel record.
    sh.render.world_quads{
        points = {x - half, y, z + half, x + half, y, z + half, x - half, y, z - half, x + half, y, z - half},
        quads = {{corners = {1, 2, 3, 4}, uv = {224, row, 255, row, 224, row + 31, 255, row + 31},
                  tint = level, screen_test = 4, precise = {1, 2, 3}}},
        texture = SPARKLE_TEXTURE, blend = "sub", depth_bias = 128, clip = CLIP,
    }
end

function effect_slot_draw(slot, type)
    if type ~= SPARKLE_SLOT then
        return
    end
    local x, _, z = sh.player_pos()
    if x > -327680 or z < 0 then   -- only west of -80 m and north of 0
        return
    end
    local light = sh.world.light_flags()
    if light & 2 ~= 0 and light & 1 ~= 0 then
        return
    end
    local row = 160 + slot_get(slot, "u8_2") * 32
    if slot_get(slot, "variant") ~= 0 then
        draw_sparkle(slot, row)
    else
        draw_grid(row)
    end
end
