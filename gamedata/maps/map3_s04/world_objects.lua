-- World objects of map3_s04: the six elevator shaft doors. The elevator
-- panel in events.lua slides them open through the map variable sliding_doors (bound to these objects); in
-- gameplay, with the player in the shaft's cell, they are closed again (z back to 59.1 m), as the map's update
-- did before drawing them.

local CELL = 163840   -- 40 m
local DOOR_Z = 242073 -- 59.1 m
local DOORS = {"ElevatorDoor1Left", "ElevatorDoor1Right", "ElevatorDoor3Left", "ElevatorDoor3Right",
               "ElevatorDoor2Left", "ElevatorDoor2Right"}

-- The cell index the C code computes: integer division truncates toward zero.
local function cell(p)
    local c = p // CELL
    if p < 0 and c * CELL ~= p then
        c = c + 1
    end
    return c
end

-- The map's projected cell: +17 for a positive position, +15 otherwise.
local function projected(p)
    if p > 0 then
        return cell(p) + 17
    end
    return cell(p) + 15
end

function update(dt)
    local x, _, z = sh.player_pos()
    if projected(x) ~= 17 or projected(z) ~= 18 or sh.event.sys_state() ~= 0 then
        return
    end
    local _, _, first_z = sh.world_object.position_get(DOORS[1])
    if first_z == DOOR_Z then
        return
    end
    for _, name in ipairs(DOORS) do
        local dx, dy = sh.world_object.position_get(name)
        sh.world_object.position_set(name, dx, dy, DOOR_Z)
    end
end

-- Per-tick sound rules of the map.
local SFX_NO_DIST_ATTEN = 2

-- The map's chunk test (PLAYER_IN_MAP_CHUNK): the index truncates toward zero; the offsets differ by sign.
local function in_chunk(p, off_pos, want_pos, off_neg, want_neg)
    local idx = cell(p)
    if p > 0 then
        return idx + off_pos == want_pos
    end
    return idx + off_neg == want_neg
end

local function sound_rules()
    local x, _, z = sh.player_pos()
    if in_chunk(x, 1, 3, -1, 3) and in_chunk(z, 1, 2, -1, 2) and not sh.flag_get("M3S04_FirstScareSoundPlayed") then
        if x > 415744 and z > 241664 then -- 101.5 m, 59.0 m
            sh.sfx_flags_play("hospital_scare_noise", 393216, -4915, 244940, 255, SFX_NO_DIST_ATTEN)
            sh.flag_set("M3S04_FirstScareSoundPlayed", true)
            local states = sh.savegame.enemy_state_get()
            if states & (1 << 2) ~= 0 then
                sh.savegame.enemy_state_set(states & ~(1 << 3))
            end
        end
    end
    if in_chunk(x, 1, 4, -1, 4) and in_chunk(z, 1, 3, -1, 3) and not sh.flag_get("M3S04_SecondScareSoundPlayed") then
        if sh.flag_get("M3S04_SecondScareSoundArmed") and z > 425984 then -- 104.0 m
            if sh.view.aabb_visible(569344, 581632, -12288, 0, 404684, 405504) == 0 then
                sh.sfx_flags_play("distant_scream", 575488, -7372, 401408, 255, SFX_NO_DIST_ATTEN)
                sh.flag_set("M3S04_SecondScareSoundPlayed", true)
            end
        end
        if z < 409600 then -- 100.0 m
            sh.flag_set("M3S04_SecondScareSoundArmed", true)
        end
    end
    if sh.pad.button("R3") == 1 then
        sh.sd_call("lisa_footstep")
    end
end

local update_doors = update
function update(dt)
    update_doors(dt)
    sound_rules()
end

-- The map's npcSpawnEvent, before the room's spawn pass: once the first stinger has played
-- (M3S04_FirstScareSoundPlayed), two Puppet Doctors join the spawn table (SpawnFlag_0).
function npc_spawn()
    if sh.flag_get("M3S04_FirstScareSoundPlayed") then
        sh.chara.spawn_flags_set("PuppetDoctor", 2, 1)
        sh.chara.spawn_flags_set("PuppetDoctor", 3, 1)
    end
end
