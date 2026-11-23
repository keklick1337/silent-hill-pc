-- World objects of map2_s00: the drawbridge (both ends) is drawn at the
-- map variable bridge_rot and the swinging door with its two hinge chains at swing_door_rot. The overlay keeps
-- them as shared rotations because events.lua writes them (the bridge switch and the door scene); the setup
-- below sets their start (bridge raised until M2S00_DrawbridgeLowered, door ajar after M2S00_PadlockBroken).

local BRIDGE = {"BridgeAxleWest", "BridgeWest", "BridgeAxleEast", "BridgeEast"}
local DOOR = {"SwingingDoor", "ChainBroken1", "ChainBroken2"}

local function follow(names, var)
    local x, y, z = sh.map.get(var .. ".x"), sh.map.get(var .. ".y"), sh.map.get(var .. ".z")
    for _, name in ipairs(names) do
        sh.world_object.rotation_set(name, x, y, z)
    end
end

local function apply()
    follow(BRIDGE, "bridge_rot")
    follow(DOOR, "swing_door_rot")
end

-- Setup when the map loads, in the original's order (the engine has applied the spawn variant of the street
-- before it).
local NO_ENEMY_SPAWN = 0x10
sh.map.set("bridge_switch_state", 0)
for _, var in ipairs({"bridge_rot", "swing_door_rot"}) do
    sh.map.set(var .. ".x", 0)
    sh.map.set(var .. ".y", 0)
    sh.map.set(var .. ".z", 0)
end
-- no enemies before the town's first event, nor between the water-works call and its cutscene
if not sh.flag_get("M2S00_CafeExitCutsceneSeen") or (sh.flag_get("M2S02_LeftForWaterWorks") and not sh.flag_get("M2S00_WaterWorksCutscene")) then
    sh.game.set_sys_flags(sh.game.sys_flags() | NO_ENEMY_SPAWN)
end
if sh.flag_get("M2S00_DarknessFell") then
    sh.map.set("eclipse_darkness", 409600) -- 100.0
end
if not sh.flag_get("M2S00_DrawbridgeLowered") then
    sh.map.set("bridge_rot.z", -568) -- -49.94 degrees: the drawbridge is raised
end
if sh.flag_get("M2S00_PadlockBroken") then
    sh.map.set("swing_door_rot.y", 91) -- 8 degrees: the door is ajar
end
if sh.flag_get("M1S05_SplitHeadDefeatSceneDone") and not sh.flag_get("M2S00_KGordonDoorOpen") then
    -- this map's enemy states lose bits 8, 26 and 27
    sh.savegame.enemy_state_set(sh.savegame.enemy_state_get() & 0xF3FFFEFF)
end
do
    local difficulty = sh.save.game_difficulty()
    local id = difficulty == -1 and 2 or (difficulty == 0 and 3 or 4)
    sh.game.set_npc_flags_id(id + 1)
    sh.game.set_npc_flags(sh.game.npc_flags() | (1 << id))
end

apply()

function update(dt)
    apply()
end

-- The per-tick rules after the draw: the drawbridge and
-- its machinery, the motor hum, the map effects of the street and the map marks. Three cell tests of the
-- original, each on the player's position; the engine saves the spawn variants' state after this.

local CELL = 163840 -- 40 m
local COLLISION_ALL = 0xFFFF
local MOTOR_SFX = "bridge_switch_motor"
-- The motor hum sources: (-43.0, -1.0, 346.0) m (the bridge) and (237.0, -1.0, 346.0) m (the control room).
local BRIDGE_HUM = {-176128, -4096, 1417216}
local ROOM_HUM = {970752, -4096, 1417216}
local HUM_FULL = 3072 -- 0.75
-- The bridge hum's volume (Q12): the original keeps it outside the map's reset data, so it lasts the
-- whole process (a later visit starts its fade from where an earlier one left it).
local HUM_KEY = "map2_s00.bridge_hum_volume"
local NO_VALUE = -1

-- The map effects preset applied last (0 = none yet since the map load).
local effects = 0

-- The original's cell: the index truncates toward zero, then +17 above zero and +15 at or below.
local function cell(p)
    local c = p // CELL
    if p < 0 and c * CELL ~= p then
        c = c + 1
    end
    return p > 0 and c + 17 or c + 15
end

local function cell_xz(x, z)
    return x + (z << 8)
end

local function effects_set(idx0, idx1, mark)
    sh.world.effects_update(idx0, idx1)
    effects = mark
end

function post_draw()
    local x, _, z = sh.player_pos()
    local here = cell_xz(cell(x), cell(z))
    local coll = 0

    if here == cell_xz(21, 13) or here == cell_xz(21, 14) or here == cell_xz(22, 14) or here == cell_xz(22, 13) then
        if here == cell_xz(21, 13) then
            -- the drawbridge's raised half collides until it came down; the street end collides until then
            if sh.flag_get("M2S00_AtBridgeControlDoor") then
                coll = coll | 4
                if x > 811008 then -- 198.0 m
                    sh.flag_set("M2S00_AtBridgeControlDoor", false)
                end
            else
                coll = coll | 2
            end
        end
        if not sh.flag_get("M2S00_DrawbridgeLowered") then
            coll = coll | 8
        end
        sh.flag_set("MapMark_OldTown_BotBridgeArrowBlank", not sh.flag_get("MapMark_OldTown_BotBridgeCorrection"))
    end

    local hum_id = sh.sound.id(MOTOR_SFX)
    local state = sh.map.get("bridge_switch_state")
    if here == cell_xz(14, 25) then
        if sh.flag_get("M2S00_DrawbridgeKeyUsed") then
            local vol = sh.session.get(HUM_KEY)
            if state == 0 then
                sh.map.set("bridge_switch_state", 1)
                sh.sd_call(MOTOR_SFX)
                vol = HUM_FULL
            elseif state == NO_VALUE then
                -- Q12_MULT_PRECISE(dt, 0.25): the arithmetic shift right is a floor division
                vol = vol + (sh.game.delta_time() * 1024) // 4096
                if vol > HUM_FULL then
                    vol = HUM_FULL
                    sh.map.set("bridge_switch_state", 1)
                end
            end
            sh.session.set(HUM_KEY, vol)
            sh.sound.play_3d_falloff(hum_id, BRIDGE_HUM[1], BRIDGE_HUM[2], BRIDGE_HUM[3], vol >> 5, 131072, 0)
        end
    elseif here == cell_xz(22, 25) then
        if sh.flag_get("M2S00_DrawbridgeKeyUsed") then
            if state == 0 then
                sh.map.set("bridge_switch_state", 1)
                sh.sd_call(MOTOR_SFX)
            end
            sh.sound.play_3d_falloff(hum_id, ROOM_HUM[1], ROOM_HUM[2], ROOM_HUM[3], 128, 65536, 0)
        end
        sh.flag_set("M2S00_RockDrillNotTaken", not sh.flag_get("M2S00_PickupRockDrill"))
    elseif state ~= 0 then
        sh.map.set("bridge_switch_state", 0)
        sh.sfx_stop(MOTOR_SFX)
    end

    if here == cell_xz(22, 25) then
        if effects ~= 2 then
            effects_set(6, 3, 2)
            sh.light.flashlight_off()
        end
    elseif here == cell_xz(19, 22) or here == cell_xz(20, 22) then
        if not sh.flag_get("M1S06_WokeUpAfterBoss") and sh.flag_get("M2S00_DarknessFell") then
            if effects ~= 1 then
                effects_set(6, 3, 1)
            end
        elseif effects ~= 3 then
            effects_set(13, 13, 3)
        end
    elseif here == cell_xz(19, 24) or here == cell_xz(20, 24) then
        if effects ~= 3 then
            effects_set(13, 13, 3)
        end
    elseif here == cell_xz(13, 13) or here == cell_xz(14, 13) then
        sh.flag_set("M2S00_ChainsawNotTaken", not sh.flag_get("M2S00_PickupChainsaw"))
    elseif effects ~= 1 then
        if sh.flag_get("M1S06_WokeUpAfterBoss") or not sh.flag_get("M2S00_DarknessFell") then
            sh.world.effects_update(1, 1)
            sh.light.flashlight_off()
        else
            sh.world.effects_update(6, 3)
        end
        effects = 1
    end

    if sh.flag_get("MapMark_OldTown_BradburyStLeftCross") and sh.flag_get("MapMark_OldTown_BachmanRdBotCross")
        and sh.flag_get("MapMark_OldTown_ElroyStBotCross") then
        sh.flag_set("MapMark_OldTown_BotRightHugeComboCross", true)
    end
    if sh.flag_get("MapMark_OldTown_BlochStLeftCross") and sh.flag_get("MapMark_OldTown_LevinStTopCross")
        and sh.flag_get("MapMark_OldTown_BlochStRightCross") and sh.flag_get("MapMark_OldTown_LevinStBotCross") then
        sh.flag_set("MapMark_OldTown_LevinBlochBigCombo", true)
    end

    sh.collision.flag_bits_clear(COLLISION_ALL)
    sh.collision.flag_bits_set(coll)
end
