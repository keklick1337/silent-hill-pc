-- World objects of map3_s03.
-- ElevatorDoor*: the elevator panel in events.lua slides them open through the map variable sliding_doors;
-- in gameplay, with the player at the shaft, they are closed again (z back to 59.1 m), as the map's update
-- did before drawing them.
-- Color*: the four colour plates of the plate door. Each has four flags (where it was put); with none of them
-- set it is not drawn, otherwise two pairs of them pick its x and its y.

local CELL = 163840   -- 40 m
local DOOR_Z = 242073 -- 59.1 m
local DOORS = {"ElevatorDoor1Left", "ElevatorDoor1Right", "ElevatorDoor3Left", "ElevatorDoor3Right",
               "ElevatorDoor2Left", "ElevatorDoor2Right"}

local COLOR_Z = 573440 -- 140.0 m
-- flags: the four flags; x_flags/y_flags: either of them set picks x_set/y_set, else x_clear/y_clear.
local COLORS = {
    {name = "ColorBlue", flags = {"M3S03_PlateOfTurtleInSlot0", "M3S03_PlateOfTurtleInSlot1", "M3S03_PlateOfTurtleInSlot2", "M3S03_PlateOfTurtleInSlot3"},
     x_flags = {"M3S03_PlateOfTurtleInSlot0", "M3S03_PlateOfTurtleInSlot1"}, x_set = -81920, x_clear = -80281, -- -20.0 / -19.6 m
     y_flags = {"M3S03_PlateOfTurtleInSlot0", "M3S03_PlateOfTurtleInSlot3"}, y_set = 0, y_clear = 1638},       -- 0.0 / 0.4 m
    {name = "ColorGreen", flags = {"M3S03_PlateOfHatterInSlot0", "M3S03_PlateOfHatterInSlot1", "M3S03_PlateOfHatterInSlot2", "M3S03_PlateOfHatterInSlot3"},
     x_flags = {"M3S03_PlateOfHatterInSlot0", "M3S03_PlateOfHatterInSlot1"}, x_set = -81920, x_clear = -80281, -- -20.0 / -19.6 m
     y_flags = {"M3S03_PlateOfHatterInSlot1", "M3S03_PlateOfHatterInSlot2"}, y_set = 0, y_clear = -1638},      -- 0.0 / -0.4 m
    {name = "ColorYellow", flags = {"M3S03_PlateOfCatInSlot0", "M3S03_PlateOfCatInSlot1", "M3S03_PlateOfCatInSlot2", "M3S03_PlateOfCatInSlot3"},
     x_flags = {"M3S03_PlateOfCatInSlot2", "M3S03_PlateOfCatInSlot3"}, x_set = -81920, x_clear = -83558, -- -20.0 / -20.4 m
     y_flags = {"M3S03_PlateOfCatInSlot1", "M3S03_PlateOfCatInSlot2"}, y_set = 0, y_clear = -1638},      -- 0.0 / -0.4 m
    {name = "ColorRed", flags = {"M3S03_PlateOfQueenInSlot0", "M3S03_PlateOfQueenInSlot1", "M3S03_PlateOfQueenInSlot2", "M3S03_PlateOfQueenInSlot3"},
     x_flags = {"M3S03_PlateOfQueenInSlot2", "M3S03_PlateOfQueenInSlot3"}, x_set = -81920, x_clear = -83558, -- -20.0 / -20.4 m
     y_flags = {"M3S03_PlateOfQueenInSlot0", "M3S03_PlateOfQueenInSlot3"}, y_set = 0, y_clear = 1638},       -- 0.0 / 0.4 m
}

local function any(flags)
    for _, f in ipairs(flags) do
        if sh.flag_get(f) then
            return true
        end
    end
    return false
end

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

local function close_doors()
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

local function place_colors()
    for _, c in ipairs(COLORS) do
        if any(c.flags) then
            local x = any(c.x_flags) and c.x_set or c.x_clear
            local y = any(c.y_flags) and c.y_set or c.y_clear
            sh.world_object.position_set(c.name, x, y, COLOR_Z)
            sh.world_object.visible_set(c.name, nil)
        else
            sh.world_object.visible_set(c.name, false)
        end
    end
end

-- Per-tick rules of the map.
local SFX_NO_DIST_ATTEN = 2
local blood_latch = false  -- the blood marks at the plate room are placed once per visit
local sound_latch = false  -- the player is in the plate room (the two sounds stop when he leaves)

-- The map's chunk test (PLAYER_IN_MAP_CHUNK): the index truncates toward zero; the offsets differ by sign.
local function in_chunk(p, off_pos, want_pos, off_neg, want_neg)
    local idx = cell(p)
    if p > 0 then
        return idx + off_pos == want_pos
    end
    return idx + off_neg == want_neg
end

local function map_rules()
    local x, _, z = sh.player_pos()
    local flags = 0
    local in_room = in_chunk(x, 1, -4, -1, -4) and in_chunk(z, 1, 2, -1, 2)
    if not in_room then
        blood_latch = false
    end
    local cell_xz = projected(x) + projected(z) * 256
    if cell_xz == 12 + 18 * 256 then
        local plate = sh.flag_get("M3S03_BloodPackPoured")
        flags = flags | (plate and 4 or 2)
        if not blood_latch then
            blood_latch = true
            sh.math_point_from_angle(-579584, 0, 251494, 0x48, 15, 1)
            sh.math_point_from_angle(-579584, 0, 251289, 0x28, 15, 1)
            if plate then
                sh.math_point_from_angle(-580403, 0, 246579, 0x6E, 15, 1)
                sh.math_point_from_angle(-580403, 0, 246579, 0x64, 15, 1)
            end
        end
    elseif cell_xz == 12 + 15 * 256 then
        if not sh.flag_get("M3S03_TurtlePlateSoundPlayed") and sh.flag_get("M3S03_PickupPlateOfTurtle") and z > -74547 then
            sh.sfx_flags_play("distant_scream", -573440, -4915, -96256, 255, SFX_NO_DIST_ATTEN)
            sh.flag_set("M3S03_TurtlePlateSoundPlayed", true)
        end
    end
    sh.collision.flag_bits_clear(0xFFFF)
    sh.collision.flag_bits_set(flags)
    if in_room then
        sound_latch = true
    elseif sound_latch then
        sh.sfx_stop("bloodsucker_idle_loop")
        sh.sfx_stop("bloodsucker_attack_loop")
        sound_latch = false
    end
end

function update(dt)
    close_doors()
    place_colors()
    map_rules()
end
