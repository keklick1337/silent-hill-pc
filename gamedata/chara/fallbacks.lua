-- What a character uses when the map it stands on does not have its data.
-- The engine runs this file after common.lua and combat.lua in every behaviour state. A mod may replace it
-- (mods/<mod>/chara/fallbacks.lua).
--
-- A character spawned by the map's room spawner has a spawn row of that map (home position, spawn flags,
-- its neighbours to call); one spawned by the console, the debug menu or a script's create (engine_spawn)
-- has none, and on a foreign map the map's arrays and story flags are not its own. A behaviour asks for
-- such data through map_data(c, need, real, ...): real(...) when the map provides it, else the fallback.
-- The fallback is the behaviour's own `fallbacks[need]` (a value, or a function(c, ...) returning the
-- values), else FALLBACK_DEFAULTS[need] below. Example (in a behavior.lua):
--
--     fallbacks = { home = function(c) return 0, 0, 0 end, reinforcements = false }
--     local hx, hy, hz = map_data(c, "home", sh.entity.spawn_point, c.spawn_index)

-- Positions spawned characters were first seen at, per slot (the generic "home").
local spawn_positions = {}

-- Where the character was spawned: the position it had when this was first asked after its spawn.
function spawn_position(c)
    local p = spawn_positions[c.slot]
    if p == nil then
        p = {c.pos_x, c.pos_y, c.pos_z}
        spawn_positions[c.slot] = p
    end
    return p[1], p[2], p[3]
end

-- A new spawn in the slot: its position is asked for anew.
function forget_spawn_position(c)
    spawn_positions[c.slot] = nil
end

FALLBACK_DEFAULTS = {
    -- Home of a character (the point it guards or returns to): where it was spawned.
    home = function(c) return spawn_position(c) end,
    -- Flags of the character's spawn row: no row.
    spawn_flags = 0,
    -- Calling others of its kind from the map's spawn rows: there are none to call.
    reinforcements = false,
    -- Recording the kill in the savegame (the row stays empty on the next visit): the spawn has no row.
    defeat_record = false,
}

-- A behaviour's own fallbacks (it may assign the whole table).
fallbacks = fallbacks or {}

-- True when the map provides the character's data: a spawn of the map's own rows.
function map_provides(c)
    return not engine_spawn(c) and c.spawn_index >= 0 and c.spawn_index < 32
end

local function fallback_of(c, need, ...)
    local f = fallbacks[need]
    if f == nil then
        f = FALLBACK_DEFAULTS[need]
    end
    if type(f) == "function" then
        return f(c, ...)
    end
    return f
end

function map_data(c, need, real, ...)
    if map_provides(c) then
        return real(...)
    end
    return fallback_of(c, need, ...)
end

-- Data of the map itself, not of a spawn row (its areas and coordinates, written into the code of the maps that
-- had this character): real(...) on one of `maps` (a set: {map5_s00 = true}) whoever spawned the character there,
-- else the fallback as in map_data.
function map_area_data(c, need, maps, real, ...)
    if maps[sh.map_name()] then
        return real(...)
    end
    return fallback_of(c, need, ...)
end
