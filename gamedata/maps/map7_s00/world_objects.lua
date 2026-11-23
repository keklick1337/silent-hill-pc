-- World objects of map7_s00: the elevator door halves stand at the
-- z of the map variable elevator_door_z (events.lua slides them in the "I'm being summoned" scene).
-- The outer pair (doors 3) and the inner pair (doors 1) share elements 0 and 1, doors 2 use 2 and 3.
-- Below them: the map's set-up and the hum of Lisa's room.

local DOORS = {
    {"Door3Right", 0}, {"Door3Left", 1},
    {"Door1Right", 0}, {"Door1Left", 1},
    {"Door2Right", 2}, {"Door2Left", 3},
}

local DOORS_CLOSED_Z = -562995 -- Q12(-137.45)
local HUM_SFX = "lisa_cry"
local CELL = 163840 -- 40 m

-- Set-up when the map loads, in the original's order. The hum volume (room_hum_sfx_volume) lives
-- outside the map's reset data, so it is only set here when the hum is already on.
for i = 3, 0, -1 do
    sh.map.set("elevator_door_z", DOORS_CLOSED_Z, i)
end
if sh.flag_get("M7S00_CutsceneLisaHospitalDone") then
    sh.sd_call(HUM_SFX)
    sh.map.set("room_hum_sfx_volume", 4096) -- 1.0
end
sh.flag_set("HospitalElevatorGoingTo3F", false)
if sh.flag_get("M2S04_SeenPoliceChalkboard") and sh.flag_get("M4S01_InspectedChalice") then
    sh.flag_set("M7S00_BotanyBookReadable", true)
end

-- The hum's volume falls with room_hum_sfx_volume (Q12; >> 4 is an arithmetic shift), never below 0x20;
-- the pitch sways with one random number per call.
local function hum()
    local v = sh.map.get("room_hum_sfx_volume") // 16
    local vol = (0xFF - v >= 0x20) and ~v or 0x20
    sh.sfx_attributes_update(HUM_SFX, 0, vol, sh.rng_uint(-8, 7))
end

function update(dt)
    for _, d in ipairs(DOORS) do
        local x, y = sh.world_object.position_get(d[1])
        sh.world_object.position_set(d[1], x, y, sh.map.get("elevator_door_z", d[2]))
    end

    -- The original's 40 m cell tests (the index truncates toward zero): Lisa's room is x in [40, 80) m,
    -- the corridor before it x in (0, 40) m, both at z in (-160, -120] m.
    local x, _, z = sh.player_pos()
    local in_row = z <= 0 and z > -4 * CELL and z <= -3 * CELL
    if in_row and x >= CELL and x < 2 * CELL then
        hum()
    end
    if in_row and x > 0 and x < CELL and sh.flag_get("M7S00_CutsceneLisaHospitalDone") then
        hum()
    end
end
